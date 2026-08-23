#include "EchoServerPch.h"

#include "EchoServer/Application/FEchoServerBootstrap.h"

int main(
	int argc,
	char* argv[])
{
	return EchoServer::Application::RunEchoServer(argc, argv);
}
