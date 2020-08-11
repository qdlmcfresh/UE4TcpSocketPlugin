// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sockets.h"
#include <Runtime\Networking\Public\Networking.h>
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Async/Async.h"
#include "HAL/UnrealMemory.h"
#include "TcpSocketConnection.h"
#include "TcpSocketServer.generated.h"

DECLARE_DYNAMIC_DELEGATE_OneParam(FTcpServerSocketDisconnectDelegate, int32, ConnectionId);
DECLARE_DYNAMIC_DELEGATE_OneParam(FTcpServerSocketConnectDelegate, int32, ConnectionId);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FTcpServerSocketReceivedMessageDelegate, int32, ConnectionId, UPARAM(ref) TArray<uint8>&, Message);

UCLASS(Blueprintable, BlueprintType)
class TCPSOCKETPLUGIN_API ATcpSocketServer : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ATcpSocketServer();
	~ATcpSocketServer();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "SocketServer")
		void Create(const FString& host, const FTcpServerSocketDisconnectDelegate& OnDisconnected, const FTcpServerSocketConnectDelegate& OnConnected,
			const FTcpServerSocketReceivedMessageDelegate& OnMessageReceived, int32& ConnectionId);
	UFUNCTION(BlueprintCallable, Category = "SocketServer")
		void Listen();
	UFUNCTION(BlueprintCallable, Category = "SocketServer")
		void Disconnect();
	/* False means we're not connected to socket and the data wasn't sent. "True" doesn't guarantee that it was successfully sent,
only that we were still connected when we initiating the sending process. */
	UFUNCTION(BlueprintCallable, Category = "Socket") // use meta to set first default param to 0
		bool SendData(int32 ConnectionId, TArray<uint8> DataToSend);


	//UFUNCTION(Category = "Socket")	
	void ExecuteOnConnected(int32 WorkerId, TWeakObjectPtr<ATcpSocketServer> thisObj);

	//UFUNCTION(Category = "Socket")
	void ExecuteOnDisconnected(int32 WorkerId, TWeakObjectPtr<ATcpSocketServer> thisObj);

	//UFUNCTION(Category = "Socket")
	void ExecuteOnMessageReceived(int32 ConnectionId, TWeakObjectPtr<ATcpSocketServer> thisObj);


	int getNum();
	void addWorker(int n, TSharedRef<class FTcpServerSocketWorker> w);
	TMap<int32, TSharedRef<class FTcpServerSocketWorker>> TcpWorkers = TMap<int32, TSharedRef<class FTcpServerSocketWorker>>();
	FThreadSafeBool* workerRun;
private:
	FTcpServerSocketDisconnectDelegate DisconnectedDelegate;
	FTcpServerSocketConnectDelegate ConnectedDelegate;
	FTcpServerSocketReceivedMessageDelegate MessageReceivedDelegate;

};

class FTcpServerAcceptWorker : public FRunnable, public TSharedFromThis<FTcpServerAcceptWorker> {
	FRunnableThread* Thread = nullptr;

private:
	FSocket* Socket;
	TWeakObjectPtr<ATcpSocketServer> ThreadSpawnerActor;
	TSharedPtr<ATcpSocketServer> Actor;
	FIPv4Endpoint endpoint;
	FThreadSafeBool bRun = false;
	FThreadSafeBool bParsed = false;
public:
	FTcpServerAcceptWorker(const FString& host, TWeakObjectPtr<ATcpSocketServer> owner);
	FTcpServerAcceptWorker(const FString& host, TWeakObjectPtr<ATcpSocketServer> owner, TSharedPtr<ATcpSocketServer> actor);
	~FTcpServerAcceptWorker();

	/*  Starts processing of the connection. Needs to be called immediately after construction	 */
	void Start();

	// Begin FRunnable interface.
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	// End FRunnable interface
};

class FTcpServerSocketWorker : public FRunnable, public TSharedFromThis<FTcpServerSocketWorker>
{

	/** Thread to run the worker FRunnable on */
	FRunnableThread* Thread = nullptr;

private:
	FSocket* Socket;
	TWeakObjectPtr<ATcpSocketServer> ThreadSpawnerActor;
	int32 id;
	int32 RecvBufferSize;
	int32 ActualRecvBufferSize;
	int32 SendBufferSize;
	int32 ActualSendBufferSize;
	float TimeBetweenTicks;
	bool bConnected = false;

	// SPSC = single producer, single consumer.
	TQueue<TArray<uint8>, EQueueMode::Spsc> Inbox; // Messages we read from socket and send to main thread. Runner thread is producer, main thread is consumer.
	TQueue<TArray<uint8>, EQueueMode::Spsc> Outbox; // Messages to send to socket from main thread. Main thread is producer, runner thread is consumer.

public:

	//Constructor / Destructor
	FTcpServerSocketWorker(FSocket* socket, TWeakObjectPtr<ATcpSocketServer> InOwner, int32 inId, int32 inRecvBufferSize, int32 inSendBufferSize, float inTimeBetweenTicks);
	virtual ~FTcpServerSocketWorker();

	/*  Starts processing of the connection. Needs to be called immediately after construction	 */
	void Start();

	/* Adds a message to the outgoing message queue */
	void AddToOutbox(TArray<uint8> Message);

	/* Reads a message from the inbox queue */
	TArray<uint8> ReadFromInbox();

	// Begin FRunnable interface.
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	// End FRunnable interface	

	/** Shuts down the thread */
	void SocketShutdown();

	/* Getter for bConnected */
	bool isConnected();

private:
	/* Blocking send */
	bool BlockingSend(const uint8* Data, int32 BytesToSend);

	/** thread should continue running */
	FThreadSafeBool bRun = false;

	/** Critical section preventing multiple threads from sending simultaneously */
	FCriticalSection SendCriticalSection;
};
