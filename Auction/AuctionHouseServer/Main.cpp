#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Application/FAuctionHouseServerBootstrap.h"

int main(
	int argc,
	char* argv[])
{
	return AuctionHouseServer::Application::RunAuctionHouseServer(argc, argv);
}
