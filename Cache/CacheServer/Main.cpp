#include "CacheServerPch.h"

#include "CacheServer/Application/FCacheServerBootstrap.h"

int main(
	int argc,
	char* argv[])
{
	return CacheServer::Application::RunCacheServer(argc, argv);
}
