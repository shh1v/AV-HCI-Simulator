#include "EgoVehicle.h"
#include "Carla/Game/CarlaStatics.h"                // GetCurrentEpisode
#include <zmq.hpp>									// ZeroMQ plugin
#include <string>									// Raw string for ZeroMQ
#include "MsgPack/DcMsgPackReader.h"				// MsgPackReader
#include "Property/DcPropertyDatum.h"				// Datum
#include "Property/DcPropertyWriter.h"				// PropertyWriter
#include "Deserialize/DcDeserializer.h"				// Deserializer
#include "Deserialize/DcDeserializerSetup.h"		// EDcMsgPackDeserializeType

bool AEgoVehicle::EstablishVehicleStatusConnection() {
	try {
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Attempting to establish python client side connection"));
		//  Prepare our context
		VehicleStatusReceiveContext = new zmq::context_t(1);
		VehicleStatusSendContext = new zmq::context_t(1);

		// Preparing addresses
		const std::string ReceiveAddress = "tcp://localhost";
		const std::string ReceiveAddressPort = "5555";
		const std::string SendAddress = "tcp://*:5556";

		// Setup the Subscriber socket
		VehicleStatusSubscriber = new zmq::socket_t(*VehicleStatusReceiveContext, ZMQ_SUB);
		VehicleStatusPublisher = new zmq::socket_t(*VehicleStatusSendContext, ZMQ_PUB);

		// Setting 100 ms recv timeout to have non-blocking behaviour
		const int timeout = 100;  // 100 ms
		VehicleStatusSubscriber->setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
		int conflate = 1;
		VehicleStatusSubscriber->setsockopt(ZMQ_CONFLATE, &conflate, sizeof(conflate));

		// Setup default topic
		VehicleStatusSubscriber->setsockopt(ZMQ_SUBSCRIBE, "", 0);

		// Connect
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Connecting to the python client"));
		VehicleStatusSubscriber->connect(ReceiveAddress + ":" + ReceiveAddressPort);
		VehicleStatusPublisher->bind(SendAddress.c_str());
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: python client connection successful"));
	}
	catch (const std::exception& e) {
		// Log a generic error message
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Failed to connect to the python client network API"));
		FString ExceptionMessage = FString(ANSI_TO_TCHAR(e.what()));
		UE_LOG(LogTemp, Error, TEXT("Exception caught: %s"), *ExceptionMessage);
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Established connection to the python client Network API"));
	bZMQVehicleStatusConnection = true;
	return true;
}

bool AEgoVehicle::TerminateVehicleStatusConnection() {
	// Check if the connection was even established
	if (!bZMQVehicleStatusConnection) {
		UE_LOG(LogTemp, Warning, TEXT("ZeroMQ: Attempting to terminate a connection that was never established."));
		return false;
	}

	try {
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Attempting to terminate python client side connection"));

		// Close the Subscriber and Publisher sockets
		if (VehicleStatusSubscriber) {
			VehicleStatusSubscriber->close();
			delete VehicleStatusSubscriber;
			VehicleStatusSubscriber = nullptr;
		}

		if (VehicleStatusPublisher) {
			VehicleStatusPublisher->close();
			delete VehicleStatusPublisher;
			VehicleStatusPublisher = nullptr;
		}

		// Terminate the contexts
		if (VehicleStatusReceiveContext) {
			delete VehicleStatusReceiveContext;
			VehicleStatusReceiveContext = nullptr;
		}

		if (VehicleStatusSendContext) {
			delete VehicleStatusSendContext;
			VehicleStatusSendContext = nullptr;
		}

		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: python client connection terminated successfully"));
	}
	catch (const std::exception& e) {
		// Log a generic error message
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Failed to terminate the python client network API connection"));
		FString ExceptionMessage = FString(ANSI_TO_TCHAR(e.what()));
		UE_LOG(LogTemp, Error, TEXT("Exception caught: %s"), *ExceptionMessage);
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Terminated connection to the python client Network API"));
	bZMQVehicleStatusConnection = false;
	return true;
}


FDcResult AEgoVehicle::RetrieveVehicleStatus() {
	// Note: using raw C++ types in the following code as it does not interact with UE interface

	// Establish connection if not already
	if (!bZMQVehicleStatusConnection && !EstablishVehicleStatusConnection()) {
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Connection not established!"));
		return FDcResult{ FDcResult::EStatus::Error };
	}

	// Receive a message from the server
	zmq::message_t Message;
	if (!VehicleStatusSubscriber->recv(&Message)) {
		bZMQVehicleStatusDataRetrieve = false;
		return FDcResult{ FDcResult::EStatus::Error };
	}

	// Store the serialized data into a TArray
	TArray<uint8> DataArray;
	DataArray.Append(static_cast<uint8*>(Message.data()), Message.size());

	// Create a deserializer
	FDcDeserializer Deserializer;
	DcSetupMsgPackDeserializeHandlers(Deserializer, EDcMsgPackDeserializeType::Default);

	// Prepare context for this run
	FDcPropertyDatum Datum(&VehicleStatusData);
	FDcMsgPackReader Reader(FDcBlobViewData::From(DataArray));
	FDcPropertyWriter Writer(Datum);

	FDcDeserializeContext Ctx;
	Ctx.Reader = &Reader;
	Ctx.Writer = &Writer;
	Ctx.Deserializer = &Deserializer;
	DC_TRY(Ctx.Prepare());
	DC_TRY(Deserializer.Deserialize(Ctx));
	bZMQVehicleStatusDataRetrieve = true;

	// Updating old and new status
	OldVehicleStatus = CurrVehicleStatus;
	if (VehicleStatusData.vehicle_status == "ManualDrive") {
		CurrVehicleStatus = VehicleStatus::ManualDrive;
	}
	else if (VehicleStatusData.vehicle_status == "Autopilot") {
		CurrVehicleStatus = VehicleStatus::Autopilot;
	}
	else if (VehicleStatusData.vehicle_status == "PreAlertAutopilot") {
		CurrVehicleStatus = VehicleStatus::PreAlertAutopilot;
	}
	else if (VehicleStatusData.vehicle_status == "TakeOver") {
		CurrVehicleStatus = VehicleStatus::TakeOver;

	}
	else if (VehicleStatusData.vehicle_status == "TakeOverManual") {
		CurrVehicleStatus = VehicleStatus::TakeOverManual;
	}
	else if (VehicleStatusData.vehicle_status == "ResumedAutopilot") {
		CurrVehicleStatus = VehicleStatus::ResumedAutopilot;
	}
	else if (VehicleStatusData.vehicle_status == "TrialOver") {
		CurrVehicleStatus = VehicleStatus::TrialOver;
	}
	else {
		CurrVehicleStatus = VehicleStatus::Unknown;
	}

	return DcOk();
}

void AEgoVehicle::UpdateVehicleStatus(VehicleStatus NewStatus)
{
	FString VehicleStatusString;
	switch  (NewStatus) {
	case VehicleStatus::ManualDrive:
		VehicleStatusString = FString("ManualDrive");
		break;
	case VehicleStatus::Autopilot:
		VehicleStatusString = FString("Autopilot");
		break;
	case VehicleStatus::PreAlertAutopilot:
		VehicleStatusString = FString("PreAlertAutopilot");
		break;
	case VehicleStatus::TakeOver:
		VehicleStatusString = FString("TakeOver");
		break;
	case VehicleStatus::TakeOverManual:
		VehicleStatusString = FString("TakeOverManual");
		break;
	case VehicleStatus::ResumedAutopilot:
		VehicleStatusString = FString("ResumedAutopilot");
		break;
	case VehicleStatus::TrialOver:
		VehicleStatusString = FString("TrialOver");
		break;
	default:
		VehicleStatusString = FString("Unknown");
	}

	// Send the new status
	if (!bZMQVehicleStatusConnection && !EstablishVehicleStatusConnection()) {
		UE_LOG(LogTemp, Warning, TEXT("ZeroMQ: Publisher not initialized!"));
		return;
	}

	FString From = TEXT("carla");

	// Get the current timestamp.
	FDateTime CurrentTime = FDateTime::Now();
	FString TimestampWithoutMilliseconds = CurrentTime.ToString(TEXT("%d/%m/%Y %H:%M:%S"));
	int32 Milliseconds = CurrentTime.GetMillisecond();
	FString Timestamp = FString::Printf(TEXT("%s.%03d"), *TimestampWithoutMilliseconds, Milliseconds);

	// Construct the "dictionary" as an FString.
	FString DictFString = FString::Printf(TEXT("{ \"from\": \"%s\", \"timestamp\": \"%s\", \"vehicle_status\": \"%s\" }"), *From, *Timestamp, *VehicleStatusString);

	// Convert the FString to a std::string to be used with ZeroMQ.
	std::string DictStdString(TCHAR_TO_UTF8(*DictFString));

	try {
		// Send the message
		zmq::message_t Message(DictStdString.size());
		memcpy(Message.data(), DictStdString.c_str(), DictStdString.size());

		VehicleStatusPublisher->send(Message);

		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Sent message: %s"), *DictFString);

	}
	catch (...) {
		UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to send message."));
		return;
	}

	// Finally, if all successful, change the local variables
	OldVehicleStatus = CurrVehicleStatus;
	CurrVehicleStatus = NewStatus;
}

void AEgoVehicle::SendCurrVehicleStatus()
{
	FString VehicleStatusString;
	switch (CurrVehicleStatus) {
	case VehicleStatus::ManualDrive:
		VehicleStatusString = FString("ManualDrive");
		break;
	case VehicleStatus::Autopilot:
		VehicleStatusString = FString("Autopilot");
		break;
	case VehicleStatus::PreAlertAutopilot:
		VehicleStatusString = FString("PreAlertAutopilot");
		break;
	case VehicleStatus::TakeOver:
		VehicleStatusString = FString("TakeOver");
		break;
	case VehicleStatus::TakeOverManual:
		VehicleStatusString = FString("TakeOverManual");
		break;
	case VehicleStatus::ResumedAutopilot:
		VehicleStatusString = FString("ResumedAutopilot");
		break;
	case VehicleStatus::TrialOver:
		VehicleStatusString = FString("TrialOver");
		break;
	default:
		VehicleStatusString = FString("Unknown");
	}

	// Send the new status
	if (!bZMQVehicleStatusConnection && !EstablishVehicleStatusConnection()) {
		UE_LOG(LogTemp, Warning, TEXT("ZeroMQ: Publisher not initialized!"));
		return;
	}

	FString From = TEXT("carla");

	// Get the current timestamp.
	FDateTime CurrentTime = FDateTime::Now();
	FString TimestampWithoutMilliseconds = CurrentTime.ToString(TEXT("%d/%m/%Y %H:%M:%S"));
	int32 Milliseconds = CurrentTime.GetMillisecond();
	FString Timestamp = FString::Printf(TEXT("%s.%03d"), *TimestampWithoutMilliseconds, Milliseconds);

	// Construct the "dictionary" as an FString.
	FString DictFString = FString::Printf(TEXT("{ \"from\": \"%s\", \"timestamp\": \"%s\", \"vehicle_status\": \"%s\" }"), *From, *Timestamp, *VehicleStatusString);

	// Convert the FString to a std::string to be used with ZeroMQ.
	std::string DictStdString(TCHAR_TO_UTF8(*DictFString));

	try {
		// Send the message
		zmq::message_t Message(DictStdString.size());
		memcpy(Message.data(), DictStdString.c_str(), DictStdString.size());

		VehicleStatusPublisher->send(Message);
	}
	catch (...) {
		UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to send message."));
	}
}

AEgoVehicle::VehicleStatus AEgoVehicle::GetCurrVehicleStatus()
{
	return CurrVehicleStatus;
}

AEgoVehicle::VehicleStatus AEgoVehicle::GetOldVehicleStatus()
{
	return OldVehicleStatus;
}