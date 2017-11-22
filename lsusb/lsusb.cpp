// "lsusb.cpp": Definiert den Einstiegspunkt für die Konsolenanwendung.
//

#include <iostream>
#include <Windows.h>
#include <SetupAPI.h>
#include <usbioctl.h>
#include <string>

using namespace std;

//Forward declarations
void ScanHubForConnectedDevices(const HANDLE& hubHandle);
wstring GetStringDescriptor(const HANDLE& hubHandle, ULONG portIndex, UCHAR descriptorIndex);

/// <summary>
/// Entry point
/// </summary>
/// <returns></returns>
int main()
{	
	//Get handle of current process
	HWND currentHWND = GetConsoleWindow();

	//Create GUID from string
	GUID usbHubGuid;
	LPCOLESTR guidStr = L"{f18a0e88-c30c-11d0-8815-00a0c906bed8}";
	HRESULT result = CLSIDFromString(guidStr, (LPCLSID)&usbHubGuid);
	
	//Determine all USB hubs
	auto deviceInfo = SetupDiGetClassDevs(&usbHubGuid, nullptr, currentHWND, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

	DWORD bufferSize = 0;
	DWORD index = 0;
	SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
	PSP_DEVICE_INTERFACE_DETAIL_DATA deviceDetailData(nullptr);
	deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	
	//Iterate over all USB hubs
	while (SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, &usbHubGuid, index, &deviceInterfaceData))
	{
		//Get details of current USB hub
		
		//First determin the size of the data to retrieve
		auto res = SetupDiGetDeviceInterfaceDetail(deviceInfo, &deviceInterfaceData, nullptr, 0, &bufferSize, nullptr);

		if (!res && GetLastError() != ERROR_INSUFFICIENT_BUFFER) { continue; }

		deviceDetailData = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(malloc(bufferSize));
		deviceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

		//Get the actual data
		res = SetupDiGetDeviceInterfaceDetail(deviceInfo, &deviceInterfaceData, deviceDetailData, bufferSize, &bufferSize, nullptr);

		if (!res) { continue; }

		//Create handle for current hub
		auto hubHandle = CreateFile(deviceDetailData->DevicePath,
									GENERIC_WRITE,
									FILE_SHARE_WRITE,
									nullptr,
									OPEN_EXISTING,
									0,
									nullptr);

		if (hubHandle == INVALID_HANDLE_VALUE) { continue; }

		ScanHubForConnectedDevices(hubHandle);

		index++;
	}

	//Clean up
	SetupDiDestroyDeviceInfoList(deviceInfo);

	wprintf(L"\nPress any key to continue...");
	cin.ignore();

    return 0;
}

/// <summary>
/// 
/// </summary>
/// <param name="hubHandle"></param>
void ScanHubForConnectedDevices(const HANDLE& hubHandle)
{
	//Determine the number of ports over the driver interface
	USB_NODE_INFORMATION nodeInfo;
	DWORD bytesReturned;

	auto res = DeviceIoControl(hubHandle,
					IOCTL_USB_GET_NODE_INFORMATION,
					&nodeInfo,
					sizeof(nodeInfo),
					&nodeInfo,
					sizeof(nodeInfo),
					&bytesReturned,
					nullptr);

	if (!res) { return; }

	auto portCount = static_cast<int>(nodeInfo.u.HubInformation.HubDescriptor.bNumberOfPorts);

	USB_NODE_CONNECTION_INFORMATION_EX nodeInfoEx;


	//Iterate over all ports an detect connected device
	for (int i = 1; i <= portCount; ++i)
	{
		nodeInfoEx.ConnectionIndex = static_cast<ULONG>(i);

		res = DeviceIoControl(hubHandle,
							IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
							&nodeInfoEx,
							sizeof(nodeInfoEx),
							&nodeInfoEx,
							sizeof(nodeInfoEx),
							&bytesReturned,
							nullptr);

		if (!res) { continue; } //on error continue with the next port

		//Ignore port if no device is connected and if device is hub
		if (nodeInfoEx.ConnectionStatus == USB_CONNECTION_STATUS::DeviceConnected && !nodeInfoEx.DeviceIsHub)
		{
			auto manufacturer = GetStringDescriptor(hubHandle, static_cast<ULONG>(i), nodeInfoEx.DeviceDescriptor.iManufacturer);
			auto device = GetStringDescriptor(hubHandle, static_cast<ULONG>(i), nodeInfoEx.DeviceDescriptor.iProduct);
			auto serialNumber = GetStringDescriptor(hubHandle, static_cast<ULONG>(i), nodeInfoEx.DeviceDescriptor.iSerialNumber);

			wprintf(L"%s %s (%s) \n", manufacturer.c_str(), device.c_str(), serialNumber.c_str());
		}
	}
}

/// <summary>
/// 
/// </summary>
/// <param name="hubHandle"></param>
/// <param name="portIndex"></param>
/// <param name="descriptor"></param>
/// <returns></returns>
wstring GetStringDescriptor(const HANDLE& hubHandle, ULONG portIndex, UCHAR descriptorIndex)
{
	DWORD bytesReturned;
	PUSB_DESCRIPTOR_REQUEST descriptorRequest(nullptr);
	PUSB_STRING_DESCRIPTOR descriptor(nullptr);
	

	UCHAR requestBuffer[sizeof(USB_DESCRIPTOR_REQUEST) + MAXIMUM_USB_STRING_LENGTH];
	auto bufferSize = sizeof(requestBuffer);

	descriptorRequest = (PUSB_DESCRIPTOR_REQUEST)requestBuffer;
	descriptor = (PUSB_STRING_DESCRIPTOR)(descriptorRequest + 1);


	memset(descriptorRequest, 0, bufferSize);

	descriptorRequest->ConnectionIndex = portIndex;
	descriptorRequest->SetupPacket.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8) | descriptorIndex;
	descriptorRequest->SetupPacket.wIndex = 0x409; //--> US-English
	descriptorRequest->SetupPacket.wLength = (USHORT)(bufferSize - sizeof(USB_DESCRIPTOR_REQUEST));

	auto res = DeviceIoControl(hubHandle,
		IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
		descriptorRequest,
		bufferSize,
		descriptorRequest,
		bufferSize, 
		&bytesReturned,
		nullptr);
	
	if (res)
	{
		return wstring(descriptor->bString);
	}
	
	return wstring(L"");
}

