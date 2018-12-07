#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
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
  ReportWhenDone,
  Done,
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
  enum State {
    Initial,
    Working,
    Finishing,
    WaitingForStop
  };
public:
  Philosopher():
    eventQueue_(),
    greaterPeerIsHungry_(false),
    idx_(-1),
    leftPeer_(false),
    mainEventQueue_(nullptr),
    nEatenMeals_(0),
    nPostponedMeals_(0),
    rightPeer_(true),
    state_(Initial),
    workerThread_(Work, ref(*this)) {}
  void dump(ostream& os) const {
    os << " idx:" << idx_ << " nPostponedMeals:" << nPostponedMeals_ <<
      " nEatenMeals:" << nEatenMeals_;
  }
  void init(int idx, const Philosopher *peers, EventQueue& mainEventQueue);
  void join() { workerThread_.join(); }
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
  bool isDone() const { return !hungry() && !greaterPeerIsHungry_; }
  bool lessThanPeer(const Peer& peer) { return idx_ < peer.idx(); }
  void reportDone();
  void resetGreaterPeerIsHungry();
  void yieldIfNeeded(Peer &peer, Event yieldedEvent);
  void yieldToGreaterPeerIfNeeded();

  mutable EventQueue eventQueue_;
  bool greaterPeerIsHungry_;
  int idx_;
  Peer leftPeer_;
  EventQueue* mainEventQueue_;
  int nEatenMeals_;
  int nPostponedMeals_;
  Peer rightPeer_;
  State state_;
  thread workerThread_;
};

ostream&
operator<<(ostream& os, const Philosopher& p)
{
  p.dump(os);
  return os;
}

int
main(int, char*[])
{
  EventQueue mainEventQueue;
  Philosopher philosophers[N_PHILOSOPHERS];
  for (int i = 0;  i < N_PHILOSOPHERS;  ++i)
    philosophers[i].init(i, philosophers, mainEventQueue);

  for(int nTestsLeft = 1000 * 1000;  --nTestsLeft;) {
    const int idx = rand() % N_PHILOSOPHERS;
    Philosopher& philosopher = philosophers[idx];
    philosopher.postEvent(GetHungry);
  }

  for(auto& philosopher: philosophers)
    philosopher.postEvent(ReportWhenDone);

  for(int nToReport = N_PHILOSOPHERS;  nToReport > 0;  --nToReport) {
    Event e = mainEventQueue.getEvent();  // wait for all to get done
    assert(e == Done);
  }

  for(auto& philosopher: philosophers)
    philosopher.postEvent(Stop);

  for(auto& philosopher: philosophers)
   philosopher.join(); 

  for(auto& philosopher: philosophers) // print philosophers
   cout << philosopher << endl;
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
  if (state_ == Finishing && !greaterPeerIsHungry_)
    reportDone();
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
  assert(state_ == Initial);
  assert(idx >= 0);
  assert(idx <= MAX_IDX);
  idx_ = idx;
  leftPeer_.init(peers[idx == 0 ? MAX_IDX : idx - 1].eventQueue(), idx);
  rightPeer_.init(peers[idx == MAX_IDX ? 0 : idx + 1].eventQueue(), idx);
  mainEventQueue_ = &mainEventQueue;
  state_ = Working;
}

void
Philosopher::work()
{
  for(Event event = getEvent();  ;  event = getEvent()) {
    assert(event != NoEvent);
    switch(state_) {
    case Initial:
      assert(false);
      break;
    case Working:
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
      case ReportWhenDone:
        if(isDone())
          reportDone();
        else
          state_ = Finishing;
        break;
      default:
        assert(false);
      }
      break;
    case Finishing:
      switch(event) {
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
        default:
          assert(false);
      }
      break;
    case WaitingForStop:
      if (event != Stop) {
        this_thread::sleep_for(seconds(idx_ + 1));
        cout << *this << ": ERROR: WaitingForStop, event is not Stop:" << event
          << endl;
      }
      assert(event == Stop);
      return;
    }
  }
}

void
Philosopher::reportDone()
{
    mainEventQueue_->postEvent(Done);
    state_ = WaitingForStop;
}

void
Philosopher::resetGreaterPeerIsHungry()
{
  greaterPeerIsHungry_ = false;
  if(state_ == Finishing && !hungry())
    reportDone();
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
