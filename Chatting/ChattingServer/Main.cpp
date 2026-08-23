#include "ChattingServerPch.h"

#include "ChattingServer/Application/FChattingServerBootstrap.h"

int main(
	int argc,
	char* argv[])
{
	return ChattingServer::Application::RunChattingServer(argc, argv);
}
