#include <string>

using std::string;

class Analyzer
{
public:
	int distance(Val x, Val y);
	Val shift(Val x, int s);
	string sdist(Val x, Val y);

	int base_distance(Val x, Val y);
	int not_distance(Val x, Val y);
};
