// Fill out your copyright notice in the Description page of Project Settings.


#include "TcpSocketServer.h"

// Sets default values
ATcpSocketServer::ATcpSocketServer()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;


}

ATcpSocketServer::~ATcpSocketServer()
{
	UE_LOG(LogTemp, Log, TEXT("ServerObject Destroyed"));
}

// Called when the game starts or when spawned
void ATcpSocketServer::BeginPlay()
{
	Super::BeginPlay();

}

void ATcpSocketServer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	//if (acceptWorkerSocket) {
	//	acceptWorkerSocket->Close();
	//	acceptWorkerSocket->~FSocket();
	//	delete(acceptWorkerSocket);
	//	*workerRun = false;
	//}
	*workerRun = false;
	Super::EndPlay(EndPlayReason);

	/*TArray<int32> keys;
	TcpWorkers.GetKeys(keys);

	for (auto& key : keys)
	{
		Disconnect(key);
	}*/
}

// Called every frame
void ATcpSocketServer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ATcpSocketServer::Create(const FString& host, const FTcpServerSocketDisconnectDelegate& OnDisconnected, const FTcpServerSocketConnectDelegate& OnConnected,
	const FTcpServerSocketReceivedMessageDelegate& OnMessageReceived, int32& ConnectionId) {
	DisconnectedDelegate = OnDisconnected;
	ConnectedDelegate = OnConnected;
	MessageReceivedDelegate = OnMessageReceived;
	UE_LOG(LogTemp, Log, TEXT("NumTCPWorkers: %i"), TcpWorkers.Num());
	TWeakObjectPtr<ATcpSocketServer> thisWeakObjPtr = TWeakObjectPtr<ATcpSocketServer>(this);
	/*TSharedRef<FTcpServerSocketWorker> test_worker(new FTcpServerSocketWorker(nullptr, thisWeakObjPtr, 0, 16384, 16384, 0.008f));
	addWorker(0, test_worker);*/
	//FTcpServerAcceptWorker worker = FTcpServerAcceptWorker(host, thisWeakObjPtr);
	TSharedRef<FTcpServerAcceptWorker> worker = MakeShared< FTcpServerAcceptWorker>(host, thisWeakObjPtr);
	worker.Get().Start();
}

void ATcpSocketServer::Listen()
{

}

void ATcpSocketServer::Disconnect()
{
}

int ATcpSocketServer::getNum()
{
	return TcpWorkers.Num();
}

void ATcpSocketServer::addWorker(int n, TSharedRef<FTcpServerSocketWorker> w)
{
	TcpWorkers.Add(n, w);
}


bool ATcpSocketServer::SendData(int32 ConnectionId, TArray<uint8> DataToSend)
{
	UE_LOG(LogTemp, Log, TEXT("Log: trying to send on Socket %i"), ConnectionId);
	if (TcpWorkers.Contains(ConnectionId))
	{
		if (TcpWorkers[ConnectionId]->isConnected())
		{
			TcpWorkers[ConnectionId]->AddToOutbox(DataToSend);
			return true;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Log: Socket %d isn't connected"), ConnectionId);
			TcpWorkers[ConnectionId]->Stop();
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Log: SocketId %d doesn't exist"), ConnectionId);
	}
	return false;
}

void ATcpSocketServer::ExecuteOnMessageReceived(int32 ConnectionId, TWeakObjectPtr<ATcpSocketServer> thisObj)
{
	// the second check is for when we quit PIE, we may get a message about a disconnect, but it's too late to act on it, because the thread has already been killed
	if (!thisObj.IsValid())
		return;

	// how to crash:
	// 1 connect with both clients
	// 2 stop PIE
	// 3 close editor
	if (!TcpWorkers.Contains(ConnectionId)) {
		return;
	}

	TArray<uint8> msg = TcpWorkers[ConnectionId]->ReadFromInbox();
	MessageReceivedDelegate.ExecuteIfBound(ConnectionId, msg);
}
void ATcpSocketServer::ExecuteOnConnected(int32 WorkerId, TWeakObjectPtr<ATcpSocketServer> thisObj)
{
	if (!thisObj.IsValid())
		return;

	ConnectedDelegate.ExecuteIfBound(WorkerId);
}

void ATcpSocketServer::ExecuteOnDisconnected(int32 WorkerId, TWeakObjectPtr<ATcpSocketServer> thisObj)
{
	if (!thisObj.IsValid())
		return;

	if (TcpWorkers.Contains(WorkerId))
	{
		TcpWorkers.Remove(WorkerId);
	}
	DisconnectedDelegate.ExecuteIfBound(WorkerId);
}

FTcpServerAcceptWorker::FTcpServerAcceptWorker(const FString& host, TWeakObjectPtr<ATcpSocketServer> owner) : ThreadSpawnerActor(owner)
{
	bParsed = FIPv4Endpoint::Parse(host, endpoint);
}

FTcpServerAcceptWorker::FTcpServerAcceptWorker(const FString& host, TWeakObjectPtr<ATcpSocketServer> owner, TSharedPtr<ATcpSocketServer> actor)
	: ThreadSpawnerActor(owner),
	Actor(actor)
{
	bParsed = FIPv4Endpoint::Parse(host, endpoint);
}

FTcpServerAcceptWorker::~FTcpServerAcceptWorker()
{
}

void FTcpServerAcceptWorker::Start()
{
	check(!Thread && "Thread wasn't null at the start!");
	check(FPlatformProcess::SupportsMultithreading() && "This platform doesn't support multithreading!");
	if (Thread)
	{
		UE_LOG(LogTemp, Log, TEXT("Log: Thread isn't null. It's: %s"), *Thread->GetThreadName());
	}
	//TODO: Stacksize anpassen
	Thread = FRunnableThread::Create(this, TEXT("FTcpServerAcceptWorker"), 128 * 1024, TPri_Normal);
	UE_LOG(LogTemp, Log, TEXT("Log: Created thread"));
}

bool FTcpServerAcceptWorker::Init()
{
	bRun = true;
	if (bParsed) {
		UE_LOG(LogTemp, Log, TEXT("Log: Creating Listening Socket"));
		Socket = FTcpSocketBuilder(FString("TCPServerSocket"))
			.AsBlocking()
			.AsReusable()
			.BoundToEndpoint(endpoint)
			.WithReceiveBufferSize(500)
			.WithSendBufferSize(500)
			.Listening(10)
			.Build();
	}
	ThreadSpawnerActor.Get()->workerRun = &bRun;
	return true;
}

uint32 FTcpServerAcceptWorker::Run()
{
	AsyncTask(ENamedThreads::GameThread, []() {	ATcpSocketConnection::PrintToConsole("Starting Tcp server accept thread.", false); });
	while (bRun) {
		
		if (!Socket) {
			bRun = false;
			UE_LOG(LogTemp, Warning, TEXT("Warning: Socket is null"));
		}
		bool pendingConnection;
		Socket->HasPendingConnection(pendingConnection);
		if (ThreadSpawnerActor.IsValid()) {
			if (pendingConnection) {
				FSocket* newConnection = Socket->Accept(TEXT("Connection"));
				UE_LOG(LogTemp, Log, TEXT("Connected"));
				int num = ThreadSpawnerActor.Get()->getNum();
				TSharedRef<FTcpServerSocketWorker> worker(new FTcpServerSocketWorker(newConnection, ThreadSpawnerActor, num, 500, 500, 0.008f));
				ThreadSpawnerActor.Get()->addWorker(num, worker);
				AsyncTask(ENamedThreads::GameThread, [this, num]() {
					ThreadSpawnerActor.Get()->ExecuteOnConnected(num, ThreadSpawnerActor);
					});
				worker->Start();
			}
		}
	}
	Socket->Close();
	Socket->~FSocket();
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	return 1;
}

void FTcpServerAcceptWorker::Stop()
{
}

void FTcpServerAcceptWorker::Exit()
{
}

bool FTcpServerSocketWorker::isConnected()
{
	FScopeLock ScopeLock(&SendCriticalSection);
	//return bConnected;
	return Socket && Socket->GetConnectionState() == ESocketConnectionState::SCS_Connected;
}

FTcpServerSocketWorker::FTcpServerSocketWorker(FSocket* socket, TWeakObjectPtr<ATcpSocketServer> InOwner, int32 inId, int32 inRecvBufferSize, int32 inSendBufferSize, float inTimeBetweenTicks)
	: Socket(socket)
	, ThreadSpawnerActor(InOwner)
	, id(inId)
	, RecvBufferSize(inRecvBufferSize)
	, SendBufferSize(inSendBufferSize)
	, TimeBetweenTicks(inTimeBetweenTicks)
{

}

FTcpServerSocketWorker::~FTcpServerSocketWorker()
{
	AsyncTask(ENamedThreads::GameThread, []() {	ATcpSocketConnection::PrintToConsole("Tcp socket thread was destroyed.", false); });
	Stop();
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

void FTcpServerSocketWorker::Start()
{
	check(!Thread && "Thread wasn't null at the start!");
	check(FPlatformProcess::SupportsMultithreading() && "This platform doesn't support multithreading!");
	if (Thread)
	{
		UE_LOG(LogTemp, Log, TEXT("Log: Thread isn't null. It's: %s"), *Thread->GetThreadName());
	}
	Thread = FRunnableThread::Create(this, TEXT("FTcpServerSocketWorker"), 128 * 1024, TPri_Normal);
	UE_LOG(LogTemp, Log, TEXT("Log: Created thread"));
}

void FTcpServerSocketWorker::AddToOutbox(TArray<uint8> Message)
{
	Outbox.Enqueue(Message);
}

TArray<uint8> FTcpServerSocketWorker::ReadFromInbox()
{
	TArray<uint8> msg;
	Inbox.Dequeue(msg);
	return msg;
}

bool FTcpServerSocketWorker::Init()
{
	bRun = true;
	bConnected = true;
	return true;
}

uint32 FTcpServerSocketWorker::Run()
{
	AsyncTask(ENamedThreads::GameThread, []() {	ATcpSocketConnection::PrintToConsole("Starting Tcp socket thread.", false); });

	while (bRun)
	{
		FDateTime timeBeginningOfTick = FDateTime::UtcNow();
		ESocketConnectionState connectionState = Socket->GetConnectionState();
		bConnected = (connectionState == ESocketConnectionState::SCS_Connected);
		// Connect
		if (!bConnected)
		{
			UE_LOG(LogTemp, Log, TEXT("Socket %i disconnected!, Stoping Thread"), this->id);
			// stop?
			bRun = false;
		}

		if (!Socket)
		{
			AsyncTask(ENamedThreads::GameThread, []() { ATcpSocketConnection::PrintToConsole(FString::Printf(TEXT("Socket is null. TcpSocketConnection.cpp: line %d"), __LINE__), true); });
			bRun = false;
			continue;
		}

		// check if we weren't disconnected from the socket
		Socket->SetNonBlocking(true); // set to NonBlocking, because Blocking can't check for a disconnect for some reason
		int32 t_BytesRead;
		uint8 t_Dummy;
		if (!Socket->Recv(&t_Dummy, 1, t_BytesRead, ESocketReceiveFlags::Peek))
		{
			bRun = false;
			continue;
		}
		Socket->SetNonBlocking(false);	// set back to Blocking

		// if Outbox has something to send, send it
		while (!Outbox.IsEmpty())
		{
			TArray<uint8> toSend;
			Outbox.Dequeue(toSend);

			if (!BlockingSend(toSend.GetData(), toSend.Num()))
			{
				// if sending failed, stop running the thread
				bRun = false;
				continue;
			}
		}

		// if we can read something		
		uint32 PendingDataSize = 0;
		TArray<uint8> receivedData;

		int32 BytesReadTotal = 0;
		// keep going until we have no data.
		for (;;)
		{
			if (!Socket->HasPendingData(PendingDataSize))
			{
				// no messages
				break;
			}

			ATcpSocketConnection::PrintToConsole(FString::Printf(TEXT("Pending data %d"), (int32)PendingDataSize), false);

			receivedData.SetNumUninitialized(BytesReadTotal + PendingDataSize);
			int32 BytesRead = 0;
			if (!Socket->Recv(receivedData.GetData() + BytesReadTotal, 20, BytesRead))
			{
				 ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
				 int error = (int)SocketSubsystem->GetLastErrorCode();
				// error code: (int32)SocketSubsystem->GetLastErrorCode()
				AsyncTask(ENamedThreads::GameThread, []() {
					ATcpSocketConnection::PrintToConsole(FString::Printf(TEXT("In progress read failed. TcpSocketConnection.cpp: line %d"), __LINE__), true);
					});
				UE_LOG(LogTemp, Log, TEXT("ErrorCode: %i"), error);
				break;
			}
			BytesReadTotal += BytesRead;

			/* TODO: if we have more PendingData than we could read, continue the while loop so that we can send messages if we have any, and then keep recving*/
		}

		// if we received data, inform the main thread about it, so it can read TQueue
		if (receivedData.Num() != 0)
		{
			Inbox.Enqueue(receivedData);
			AsyncTask(ENamedThreads::GameThread, [this]() {
				ThreadSpawnerActor.Get()->ExecuteOnMessageReceived(id, ThreadSpawnerActor);
				});
		}

		/* In order to sleep, we will account for how much this tick took due to sending and receiving */
		FDateTime timeEndOfTick = FDateTime::UtcNow();
		FTimespan tickDuration = timeEndOfTick - timeBeginningOfTick;
		float secondsThisTickTook = tickDuration.GetTotalSeconds();
		float timeToSleep = TimeBetweenTicks - secondsThisTickTook;
		if (timeToSleep > 0.f)
		{
			//AsyncTask(ENamedThreads::GameThread, [timeToSleep]() { ATcpSocketConnection::PrintToConsole(FString::Printf(TEXT("Sleeping: %f seconds"), timeToSleep), false); });
			FPlatformProcess::Sleep(timeToSleep);
		}
	}

	bConnected = false;

	AsyncTask(ENamedThreads::GameThread, [this]() {
		ThreadSpawnerActor.Get()->ExecuteOnDisconnected(id, ThreadSpawnerActor);
		});
	AsyncTask(ENamedThreads::GameThread, [this]() { 
		UE_LOG(LogTemp, Log, TEXT("Stopping Workerthread %i"), this->id); });

	SocketShutdown();

	return 0;
}

void FTcpServerSocketWorker::Stop()
{
	bRun = false;
}

void FTcpServerSocketWorker::Exit()
{

}

bool FTcpServerSocketWorker::BlockingSend(const uint8* Data, int32 BytesToSend)
{
	if (BytesToSend > 0)
	{
		int32 BytesSent = 0;
		if (!Socket->Send(Data, BytesToSend, BytesSent))
		{
			return false;
		}
	}
	return true;
}

void FTcpServerSocketWorker::SocketShutdown()
{
	// if there is still a socket, close it so our peer will get a quick disconnect notification
	if (Socket)
	{
		Socket->Close();
	}
}
