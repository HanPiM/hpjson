#include <iostream>

#include "hpjson.hpp"

#include <map>

using namespace hpjson;

int main(int)
{
	json j;
	
	j.assign(_json());
	j.assign(10086);
	
	auto x = json::array();
	x.resize(5);
	j = x;
	x.clear();

	j =
	{
		999,222,
		{
			{
				"arr",
				{
					0,1,2,3,4,5,6,7,8,9,
					true
				}
			},
			{"123",888},
			{"246",999}
		},
		j
	};

	int d = j[2]["246"];

	std::cout << (int)j.at(2)["246"]<<' '<<d<<' '<<(int)j.at(2).at("246");

	std::cout << " " << "/2/arr/10"_json_pointer._get_from(j,false).type_name();

	//std::cout << j[3].get<json::array>().size();


	return 0;
}