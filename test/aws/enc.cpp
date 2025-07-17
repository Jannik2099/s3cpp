#include <boost/url/parse_query.hpp>
#include <iostream>

int main() { std::cout << boost::urls::parse_query("id=42&name=jane+doe&page+size=20").value(); }
