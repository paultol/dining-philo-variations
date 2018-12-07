dp: dp.cpp
	g++ -g -pthread -std=c++11 $< -o $@

dp.fast: dp.cpp
	g++ -O3 -funroll-loops -g -pthread -std=c++11 $< -o $@
