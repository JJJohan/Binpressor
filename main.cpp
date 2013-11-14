#include <iostream>
#include "Binpressor.h"

int main(int argc, char* argv[])
{
	Binpressor* binpressor = new Binpressor(argc, argv);
	delete binpressor;

	// Temp
	std::cin.clear();
	std::cin.get();

	return 0;
}