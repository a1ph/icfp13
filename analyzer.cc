#include "gen2.h"
#include "analyzer.h"

#include <sstream>

using std::stringstream;

Val Analyzer::shift(Val x, int s)
{
    if (s > 63 || s < -63) return 0;
    return s > 0 ? x << s : x >> (-s);
}

int Analyzer::distance(Val x, Val y)
{
	int min_distance = 111;
	for (int s = -64; s <= 64; s++) {
		int d = not_distance(shift(x, s), y);
	    if (d < min_distance)
	    	min_distance = d;
	}
	return min_distance;
}

int Analyzer::not_distance(Val x, Val y)
{
    int d = base_distance(x, y);
    int n = base_distance(~x, y);
    return d < n ? d : n;
}

int Analyzer::base_distance(Val x, Val y)
{
	Val v = x ^ y;
	// do the bit count
	int count;
	for (count = 0; v; count++) {
		v = (v - 1) & v;
	}
	return count;
}

string Analyzer::sdist(Val x, Val y)
{
	stringstream ss;
	int min_distance = 111;
	for (int s = -64; s <= 64; s++) {
		int d = not_distance(shift(x, s), y);
		if (s == 0)
			ss << '|';
		if (d == 0)
			ss << '.';
		else if (d < 10)
   		    ss << d;
   		else if (d <= 36)
   			ss << (char)('a' + d - 10);
   		else
   			ss << '*';
		if (s == 0)
			ss << '|';
	}
	return ss.str();
}

#ifdef AMAIN
int main()
{
	Analyzer a;
	int d = a.distance(0xb445fbb8cddcf9f8, 0xF0F0F0F0B445FBB8);
	printf("min_distance: %d\n", d);
	return 0;
}
#endif