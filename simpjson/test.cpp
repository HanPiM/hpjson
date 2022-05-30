#include <iostream>
#include <Windows.h>
#include <fstream>

#define _SJSON_DISABLE_AUTO_TYPE_ADJUST

#include "sjson.hpp"

using namespace sjson;

int main()
{

	using sjson::_sjson_detail::parser;

	std::string s = R"__( 
{
	"s",[""],
"ss",[],
"x":{"aa":bb}
},1,2,3,4,5
)__";

	

	//parser<json> p2 = parser<json>(std::cin);

	//std::cout << jjj.dump();

	//std::cout << p2.result().dump() << '\n';

	auto p = parser<json>(s.begin(), s.end());

	std::cout << p.result().dump();

	std::string s2;
	//_sjson_detail::escape_to_ascii(s, s2);
	std::cout << "\n___\n";
	const json a = {
		{"pi", 3.141},
		{"happy", true},
		{"name", "Niels"},
		{"nothing", nullptr},
		{
			"answer", {{"everything", 42}}
		},
		{"list", {1, 0, 2}},
		{
			"object",
			{
				{"currency", "USD"},
				{"value", 42.99}
			}
		}
	};

	s.clear();

	a.dump_to(s, 2, ' ', true);

	std::cout << s << '\n';

	try
	{
		std::cout << a[9].dump(0);
		//std::cout << a["sss"][0].get<int>();
	}
	catch (const std::exception& e)
	{
		std::cout << e.what();
		//MessageBoxA(NULL, e.what(), "e", MB_ICONERROR);
	}


	return 0;
}
