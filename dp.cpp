#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <queue>
#include <thread>

using namespace std;
using namespace std::chrono;

static constexpr int N_PHILOSOPHERS = 5;
static constexpr int MAX_IDX = N_PHILOSOPHERS - 1;

enum Event {
  NoEvent,
  GetHungry,
  YieldToLeftPeer,
  YieldToRightPeer,
  LeftPeerYielded,
  RightPeerYielded,
  ReportWhenNotHungry,
  NotHungry,
  Stop
};

class Philosopher;

class EventQueue {
public:
  EventQueue():
    condVar_(),
    mutex_(),
    queue_() {}
  Event getEvent() {
    Event event(NoEvent);
    unique_lock<mutex> queueUniqueLock(mutex_);
    if(queue_.empty())
      condVar_.wait(queueUniqueLock);
    assert(!queue_.empty());
    event = queue_.front();
    assert(event != NoEvent);
    queue_.pop();
    return event;
  }
  void postEvent(Event event) {
    lock_guard<mutex> queueGuard(mutex_);
    condVar_.notify_one();
    queue_.push(event);
  }
private:
  condition_variable condVar_;
  mutex mutex_;
  queue<Event> queue_;
};

class Peer {
public:
  explicit Peer(bool ownsChopstick):
    eventQueue_(nullptr),
    knowsToYield_(false),
    ownsChopstick_(ownsChopstick) {}
  void clearKnowsToYield() { knowsToYield_ = false; }
  void clearOwnsChopstick() { ownsChopstick_ = false; }
  Event getEvent() { return eventQueue_->getEvent(); }
  int idx() const { return idx_; }
  void init(EventQueue& eventQueue, int idx) {
    eventQueue_ = &eventQueue;
    idx_ = idx;
  }
  bool knowsToYield() const { return knowsToYield_; }
  bool ownsChopstick() const { return ownsChopstick_; }
  void postEvent(Event event) { eventQueue_->postEvent(event); }
  void setKnowsToYield() { knowsToYield_ = true; }
  void setOwnsChopstick() { ownsChopstick_ = true; }
private:
  EventQueue* eventQueue_;
  int idx_;
  bool knowsToYield_;
  bool ownsChopstick_;
};

class Philosopher {
public:
  Philosopher():
    eventQueue_(),
    greaterPeerIsHungry_(false),
    idx_(-1),
    leftPeer_(false),
    mainEventQueue_(nullptr),
    nEatenMeals_(0),
    needToReportNotHungry_(false),
    nPostponedMeals_(0),
    rightPeer_(true),
    workerThread_(Work, ref(*this)) {}
  void dump(ostream& os) const {
    os << " idx:" << idx_ << " nPostponedMeals:" << nPostponedMeals_ <<
      " nEatenMeals:" << nEatenMeals_;
  }
  void init(int idx, const Philosopher *peers, EventQueue& mainEventQueue);
  void join() { workerThread_.join(); }
  int nEatenMeals() const { return nEatenMeals_; }
  void postEvent(Event event) { eventQueue_.postEvent(event); }
  Event getEvent() { return eventQueue_.getEvent(); }
  static void Work(Philosopher& philosopher) { philosopher.work(); }
  void work();
private:
  bool askPeerToYieldIfNeeded(Peer& peer, Event yieldEvent);
  EventQueue& eventQueue() const { return eventQueue_; }
  void eatPostponedMeals();
  void eatTheOnlyMeal();
  void grabChopstick(Peer& peer);
  void handlePeerYielded(Peer& yieldedPeer, Peer& otherPeer,
    Event otherPeerYieldEvent);
  bool hungry() const { return !!nPostponedMeals_; }
  bool lessThanPeer(const Peer& peer) { return idx_ < peer.idx(); }
  void reportNotHungry();
  void resetGreaterPeerIsHungry();
  void yieldIfNeeded(Peer &peer, Event yieldedEvent);
  void yieldToGreaterPeerIfNeeded();

  mutable EventQueue eventQueue_;
  bool greaterPeerIsHungry_;
  int idx_;
  Peer leftPeer_;
  EventQueue* mainEventQueue_;
  int nEatenMeals_;
  bool needToReportNotHungry_;
  int nPostponedMeals_;
  Peer rightPeer_;
  thread workerThread_;
};

ostream&
operator<<(ostream& os, const Philosopher& p)
{
  p.dump(os);
  return os;
}

int
main(int argc, char *argv[])
{
  EventQueue mainEventQueue;
  Philosopher philosophers[N_PHILOSOPHERS];
  for (int i = 0;  i < N_PHILOSOPHERS;  ++i)
    philosophers[i].init(i, philosophers, mainEventQueue);

  constexpr int TOTAL_N_MEALS_DFLT = 1000 * 1000;
  const int TOTAL_N_MEALS = argc == 1 ? TOTAL_N_MEALS_DFLT : atoi(argv[1]);

  for(int nTestsLeft = TOTAL_N_MEALS;  nTestsLeft--;) {
    const int idx = rand() % N_PHILOSOPHERS;
    Philosopher& philosopher = philosophers[idx];
    philosopher.postEvent(GetHungry);
  }

  for(auto& philosopher: philosophers)
    philosopher.postEvent(ReportWhenNotHungry);

  for(int nToReport = N_PHILOSOPHERS;  nToReport > 0;  --nToReport) {
    Event e = mainEventQueue.getEvent();  // wait for all to get not hungry
    assert(e == NotHungry);
  }

  for(auto& philosopher: philosophers)
    philosopher.postEvent(Stop);

  for(auto& philosopher: philosophers)
   philosopher.join(); 

  for(auto& philosopher: philosophers) // print philosophers
   cout << philosopher << endl;

  const int nEatenMeals = accumulate(begin(philosophers), end(philosophers), 0,
    [](int a, const Philosopher& p) { return a + p.nEatenMeals(); });

  cout << "TOTAL_N_MEALS:" << TOTAL_N_MEALS << " nEatenMeals:" << nEatenMeals <<
    endl;


  return 0;
}

bool
Philosopher::askPeerToYieldIfNeeded(Peer& peer, Event yieldEvent)
{
  if (!peer.ownsChopstick())
    return false;
  if (peer.knowsToYield())
    return false;
  peer.postEvent(yieldEvent);
  peer.setKnowsToYield();
  return true;
}

void
Philosopher::eatPostponedMeals()
{
  assert(!!nPostponedMeals_);
  nEatenMeals_ += nPostponedMeals_;
  nPostponedMeals_ = 0;
  if(needToReportNotHungry_) {
    reportNotHungry();
    needToReportNotHungry_ = false;
  }
}

void
Philosopher::eatTheOnlyMeal()
{
  assert(!nPostponedMeals_);
  ++nEatenMeals_;
}

void
Philosopher::grabChopstick(Peer& peer)
{
  assert(hungry());
  peer.clearOwnsChopstick();
}

void
Philosopher::handlePeerYielded(Peer& yieldedPeer, Peer& otherPeer,
  Event otherPeerYieldEvent)
{
  yieldedPeer.clearKnowsToYield();
  grabChopstick(yieldedPeer);
  if (askPeerToYieldIfNeeded(otherPeer, otherPeerYieldEvent)) {
    assert(otherPeer.ownsChopstick());
    return;
  }
  if (otherPeer.ownsChopstick())
    return;
  eatPostponedMeals();
  yieldToGreaterPeerIfNeeded();
}

void
Philosopher::init(int idx, const Philosopher *peers, EventQueue& mainEventQueue)
{
  assert(idx >= 0);
  assert(idx <= MAX_IDX);
  idx_ = idx;
  leftPeer_.init(peers[idx == 0 ? MAX_IDX : idx - 1].eventQueue(), idx);
  rightPeer_.init(peers[idx == MAX_IDX ? 0 : idx + 1].eventQueue(), idx);
  mainEventQueue_ = &mainEventQueue;
}

void
Philosopher::work()
{
  for(Event event = getEvent();  ;  event = getEvent()) {
    assert(event != NoEvent);
      switch(event) {
      case GetHungry:
        if (askPeerToYieldIfNeeded(leftPeer_, YieldToRightPeer) ||
          askPeerToYieldIfNeeded(rightPeer_, YieldToLeftPeer)) {
          ++nPostponedMeals_;
          break;
        }
        if (rightPeer_.ownsChopstick() || leftPeer_.ownsChopstick()) {
          ++nPostponedMeals_;
          break;
        }
        eatTheOnlyMeal();
        yieldToGreaterPeerIfNeeded();
        break;
      case YieldToLeftPeer:
        yieldIfNeeded(leftPeer_, RightPeerYielded);
        break;
      case YieldToRightPeer:
        yieldIfNeeded(rightPeer_, LeftPeerYielded);
        break;
      case LeftPeerYielded:
        handlePeerYielded(leftPeer_, rightPeer_, YieldToLeftPeer);
        break;
      case RightPeerYielded:
        handlePeerYielded(rightPeer_, leftPeer_, YieldToRightPeer);
        break;
      case ReportWhenNotHungry:
        if(!hungry())
          reportNotHungry();
        else
          needToReportNotHungry_ = true;
        break;
      case Stop:
        return;
      default:
        assert(false);
      }
  }
}

void
Philosopher::reportNotHungry()
{
    mainEventQueue_->postEvent(NotHungry);
}

void
Philosopher::resetGreaterPeerIsHungry()
{
  greaterPeerIsHungry_ = false;
}

void
Philosopher::yieldIfNeeded(Peer &peer, Event yieldedEvent)
{
  if(lessThanPeer(peer)) {
    if(hungry()) {
      if(!greaterPeerIsHungry_)
        greaterPeerIsHungry_ = true;
      return; // don't yield
    }
    if(greaterPeerIsHungry_) {
      resetGreaterPeerIsHungry();
    }
  }
  if(peer.ownsChopstick()) // we yielded it before, after eating
    return;
  peer.setOwnsChopstick();
  peer.postEvent(yieldedEvent);
}

void
Philosopher::yieldToGreaterPeerIfNeeded()
{
  if(!greaterPeerIsHungry_)
    return;
  resetGreaterPeerIsHungry();
  const bool leftPeerIsGreater = lessThanPeer(leftPeer_);
  Peer& greaterPeer = leftPeerIsGreater ? leftPeer_ : rightPeer_;
  const Event yieldedEvent = leftPeerIsGreater ? RightPeerYielded :
    LeftPeerYielded;      
  greaterPeer.setOwnsChopstick();
  greaterPeer.postEvent(yieldedEvent);
}

/* TODO:
 * - could cache lessThanPeer for both peers;
*/
