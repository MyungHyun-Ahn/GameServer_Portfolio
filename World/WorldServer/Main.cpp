#include "WorldServerPch.h"

#include "WorldServer/Application/FWorldServerBootstrap.h"

int main(
	int argc,
	char* argv[])
{
	return WorldServer::Application::RunWorldServer(argc, argv);
}
