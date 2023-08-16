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
		VehicleStatusContext = new zmq::context_t(1);
		std::string Address = "tcp://localhost";
		std::string AddressPort = "5555"
		// Setup the Subscriber socket
		VehicleStatusSubscriber = new zmq::socket_t(*VehicleStatusContext, ZMQ_SUB);

		// Setting 1 ms recv timeout
		const int timeout = 100;  // 1 ms
		VehicleStatusSubscriber->setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
		// Setup default topic
		VehicleStatusSubscriber->setsockopt(ZMQ_SUBSCRIBE, "", 0);

		// Connect
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Connecting to the python client"));
		VehicleStatusSubscriber->connect(Address + ":" + AddressPort);
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: python client connection successful"));
	}
	catch (...) {
		// Log a generic error message
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Failed to connect to the python client network API"));
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Established connection to the python client Network API"));
	bZMQVehicleStatusConnection = true;
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
		bZMQVehicleStatusDataRetrive = false;
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
	bZMQVehicleStatusDataRetrive = true;
	return DcOk();
}

void AEgoVehicle::UpdateVehicleStatus()
{
	// Not possible to update the status if data is not retrived
	if (!bZMQVehicleStatusDataRetrive) {
		return;
	}

	OldVehicleStatus = CurrVehicleStatus;
	if (VehicleStatusData.vehicle_status == "ManualDrive") {
		CurrVehicleStatus = VehicleStatus::ManualDrive;
	}
	else if (VehicleStatusData.vehicle_status == "AutoPilot") {
		CurrVehicleStatus = VehicleStatus::AutoPilot;
	}
	else if (VehicleStatusData.vehicle_status == "PreAlertAutopilot") {
		CurrVehicleStatus = VehicleStatus::PreAlertAutopilot;
	}
	else if (VehicleStatusData.vehicle_status == "TakeOver") {
		CurrVehicleStatus = VehicleStatus::TakeOver;
	}
	else {
		CurrVehicleStatus = VehicleStatus::Unknown;
	}
}

void AEgoVehicle::UpdateVehicleStatus(VehicleStatus NewStatus)
{
	OldVehicleStatus = CurrVehicleStatus;
	CurrVehicleStatus = NewStatus;
}


bool AEgoVehicle::SendVehicleStatus()
{
	return true;
}

AEgoVehicle::VehicleStatus AEgoVehicle::GetCurrVehicleStatus()
{
	return CurrVehicleStatus;
}

AEgoVehicle::VehicleStatus AEgoVehicle::GetOldVehicleStatus()
{
	return OldVehicleStatus;
}