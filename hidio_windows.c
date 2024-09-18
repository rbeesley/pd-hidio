#ifdef _WIN32
/*
 *  Microsoft Windows DDK HID support for Pd/Max [hidio] object
 *
 *  Copyright (c) 2006 Olaf Matthes. All rights reserved.
 *
 *  Modified 2020 by Martin Peach <chakekatzil@gmail.com> to run on pd-0.50.0, build using Msys64.
 *
 *  Modified 2024 by Ryan Beesley <pd+hidio@beesley.name> to support get_device_number_by_id and other calls supported by Linux and MacOS.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 */

#include <windows.h>
#include <winbase.h>
#ifdef WSL
#include <winerror.h>
#else
#include <WinError.h>
#endif 
#include <stdio.h>
#ifdef WSL
#include <initguid.h>
#include <usbiodef.h>
#endif
#include <setupapi.h> 

/*
 * Please note that this file needs the Microsoft Driver Developent Kit (DDK)
 * to be installed in order to compile!
 */

#include <hidsdi.h>

#include "hidio.h"

#define CLASS_NAME "[hidio]"

typedef struct _hid_device_
{
	HANDLE					fh;	/* file handle to the hid device */
	OVERLAPPED				overlapped;

	PHIDP_PREPARSED_DATA	ppd; // The opaque parser info describing this device
	HIDP_CAPS				caps; // The Capabilities of this hid device.
	HIDD_ATTRIBUTES			attributes;
	char					*inputReportBuffer;
	unsigned long			inputDataLength; // Num elements in this array.
	PHIDP_BUTTON_CAPS		inputButtonCaps;
	PHIDP_VALUE_CAPS		inputValueCaps;

	char					*outputReportBuffer;
	unsigned long			outputDataLength;
	PHIDP_BUTTON_CAPS		outputButtonCaps;
	PHIDP_VALUE_CAPS		outputValueCaps;

	char					*featureReportBuffer;
	unsigned long			featureDataLength;
	PHIDP_BUTTON_CAPS		featureButtonCaps;
	PHIDP_VALUE_CAPS		featureValueCaps;
} t_hid_device;


/*==============================================================================
 *  GLOBAL VARS
 *======================================================================== */

extern t_int hidio_instance_count; // in hidio.h

/* store device pointers so I don't have to query them all the time */
// t_hid_devinfo device_pointer[MAX_DEVICES];

/*==============================================================================
 * FUNCTION PROTOTYPES
 *==============================================================================
 */


/*==============================================================================
 * Event TYPE/CODE CONVERSION FUNCTIONS
 *==============================================================================
 */

/* ============================================================================== */
/* WINDOWS DDK HID SPECIFIC REALLY LOW-LEVEL STUFF */
/* ============================================================================== */
/*
 *  connect to Ith USB device (count starting with 0)
 */

static HANDLE connectDeviceNumber(DWORD deviceIndex)
{
    GUID hidGUID;
    HDEVINFO hardwareDeviceInfoSet;
    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    PSP_INTERFACE_DEVICE_DETAIL_DATA deviceDetail;
    ULONG requiredSize;
    HANDLE deviceHandle = INVALID_HANDLE_VALUE;
    DWORD result;

    //Get the HID GUID value - used as mask to get list of devices
    HidD_GetHidGuid (&hidGUID);

    //Get a list of devices matching the criteria (hid interface, present)
    hardwareDeviceInfoSet = SetupDiGetClassDevs (&hidGUID,
                                                 NULL, // Define no enumerator (global)
                                                 NULL, // Define no
                                                 (DIGCF_PRESENT | // Only Devices present
                                                 DIGCF_DEVICEINTERFACE)); // Function class devices.

    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    //Go through the list and get the interface data
    result = SetupDiEnumDeviceInterfaces (hardwareDeviceInfoSet,
                                          NULL, //infoData,
                                          &hidGUID, //interfaceClassGuid,
                                          deviceIndex, 
                                          &deviceInterfaceData);

    /* Failed to get a device - possibly the index is larger than the number of devices */
    if (result == FALSE)
    {
        SetupDiDestroyDeviceInfoList (hardwareDeviceInfoSet);
		error("%s: failed to get specified device number", CLASS_NAME);
        return INVALID_HANDLE_VALUE;
    }

    //Get the details with null values to get the required size of the buffer
    SetupDiGetDeviceInterfaceDetail (hardwareDeviceInfoSet,
                                     &deviceInterfaceData,
                                     NULL, //interfaceDetail,
                                     0, //interfaceDetailSize,
                                     &requiredSize,
                                     0); //infoData))

    //Allocate the buffer
    deviceDetail = (PSP_INTERFACE_DEVICE_DETAIL_DATA)malloc(requiredSize);
    deviceDetail->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);

    //Fill the buffer with the device details
    if (!SetupDiGetDeviceInterfaceDetail (hardwareDeviceInfoSet,
                                          &deviceInterfaceData,
                                          deviceDetail,
                                          requiredSize,
                                          &requiredSize,
                                          NULL)) 
    {
        SetupDiDestroyDeviceInfoList (hardwareDeviceInfoSet);
        free (deviceDetail);
		error("%s: failed to get device info", CLASS_NAME);
        return INVALID_HANDLE_VALUE;
    }

    /* Open file on the device (read & write) */
	deviceHandle = CreateFile 
					(deviceDetail->DevicePath, 
					GENERIC_READ|GENERIC_WRITE, 
					FILE_SHARE_READ|FILE_SHARE_WRITE, 
					(LPSECURITY_ATTRIBUTES)NULL,
					OPEN_EXISTING, 
					FILE_FLAG_OVERLAPPED,
					NULL);

	if (deviceHandle == INVALID_HANDLE_VALUE)
	{
		int err = GetLastError();
		LPVOID lpMsgBuf;
		if (err == ERROR_ACCESS_DENIED)
		{
			/* error("%s: can not read from mouse and keyboard", CLASS_NAME); */
			return INVALID_HANDLE_VALUE;
		}
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf, 0, NULL);
		error("%s: could not get device #%li: %s (%d)", CLASS_NAME, deviceIndex + 1, (LPCTSTR)lpMsgBuf, err);
		LocalFree(lpMsgBuf);
 	}

    SetupDiDestroyDeviceInfoList (hardwareDeviceInfoSet);
    free (deviceDetail);
    return deviceHandle;
}

static long _hidio_read(t_hid_device *self)
{
	unsigned long ret, err = 0;
	unsigned long bytes = 0;

    if (!ReadFile(self->fh, self->inputReportBuffer, self->caps.InputReportByteLength, &bytes, (LPOVERLAPPED)&self->overlapped)) 
    {
        if (ERROR_IO_PENDING != (err = GetLastError()))
		{
			LPVOID lpMsgBuf;
			FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf, 0, NULL);
			error("%s: read: %s", CLASS_NAME, (LPCTSTR)lpMsgBuf);
			LocalFree(lpMsgBuf);
			return -1;
		}
    }

	ret = WaitForSingleObject(self->overlapped.hEvent, 0);

	if (ret == WAIT_OBJECT_0)	/* hey, we got signalled ! => data */
	{
		return bytes;
	}
	else	/* if no data, cancel read */
	{
		if (!CancelIo(self->fh) && (self->fh != INVALID_HANDLE_VALUE))
			return -1;	/* CancelIo() failed */
	}
	if (!ResetEvent(self->overlapped.hEvent))
		return -1;	/* ResetEvent() failed */

	return bytes;
}

/* count devices by looking into the registry */
short _hid_count_devices(void)
{
    short	i, gNumDevices = 0;
	long	ret;

	HKEY	hKey;
    unsigned long	DeviceNameLen, KeyNameLen;
    char	KeyName[MAXPDSTRING];
	char	DeviceName[MAXPDSTRING];

	/* Search in Windows Registry for enumerated HID devices */
    if ((ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\HidUsb\\Enum", 0, KEY_QUERY_VALUE, &hKey)) != ERROR_SUCCESS)
	{
		error("hidio: failed to get list of HID devices from registry");
        return EXIT_FAILURE;
    }

    for (i = 0; i < MAX_DEVICES + 3; i++)	/* there are three entries that are no devices */
	{
        DeviceNameLen = 80;
        KeyNameLen = 100;
		ret = RegEnumValue(hKey, i, KeyName, &KeyNameLen, NULL, NULL, (unsigned char*)DeviceName, &DeviceNameLen);
        if (ret == ERROR_SUCCESS)
		{
			if (!strncmp(KeyName, "Count", 5))
			{
				/* this is the number of devices as HEX DWORD */
				continue;
			}
			else if (!strncmp(DeviceName, "USB\\VID", 7))
			{
				/* we found a device, DeviceName contains the path */
				// post("device #%d: %s = %s", gNumDevices, KeyName, DeviceName);
				gNumDevices++;
				continue;
			}
		}
		else if (ret == ERROR_NO_MORE_ITEMS)	/* no more entries in registry */
		{
			break;
		}
		else	/* any other error while looking into registry */
		{
			char errbuf[MAXPDSTRING];
			FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, ret, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
							errbuf, MAXPDSTRING, NULL);
			error("hidio: %s", errbuf);
			break;
		}
	}
	RegCloseKey(hKey);
    return gNumDevices;		/* return number of devices */
}

/* get device path for a HID specified by number */
static short _hid_get_device_path(short device_number, char **path, short length)
{
	GUID guid;
	HDEVINFO DeviceInfo;
	SP_DEVICE_INTERFACE_DATA DeviceInterface;
	PSP_INTERFACE_DEVICE_DETAIL_DATA DeviceDetail;
	unsigned long iSize;
	
	HidD_GetHidGuid(&guid);

	DeviceInfo = SetupDiGetClassDevs(&guid, NULL, NULL, (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

	DeviceInterface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	if (!SetupDiEnumDeviceInterfaces(DeviceInfo, NULL, &guid, device_number, &DeviceInterface))
	{
		SetupDiDestroyDeviceInfoList(DeviceInfo);
		return EXIT_FAILURE;
	}

	SetupDiGetDeviceInterfaceDetail( DeviceInfo, &DeviceInterface, NULL, 0, &iSize, 0 );

	DeviceDetail = (PSP_INTERFACE_DEVICE_DETAIL_DATA)malloc(iSize);
	DeviceDetail->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);

	if (SetupDiGetDeviceInterfaceDetail(DeviceInfo, &DeviceInterface, DeviceDetail, iSize, &iSize, NULL))
	{
		if (!*path && !length)	/* we got no memory passed in, allocate some */
		{						/* WARNING: caller has to free this memory!! */
			*path = (char *)getbytes((short)(strlen(DeviceDetail->DevicePath) * sizeof(char)));
		}
		/* copy path */
		strcpy(*path, DeviceDetail->DevicePath);
	}
	free(DeviceDetail);

	SetupDiDestroyDeviceInfoList(DeviceInfo);
	return 0;
}


/* get capabilities (usage page & usage ID) of an already opened device */
static short _hid_get_capabilities(HANDLE fd, HIDP_CAPS *capabilities)
{
	PHIDP_PREPARSED_DATA	preparsedData;

	if (fd == INVALID_HANDLE_VALUE)
	{
		error("hidio: couldn't get device capabilities due to an invalid handle");
		return EXIT_FAILURE;
	}

	/* returns and allocates a buffer with device info */
	HidD_GetPreparsedData(fd, &preparsedData);

	/* get capabilities of device from above buffer */
	HidP_GetCaps(preparsedData, capabilities);

	/* no need for PreparsedData any more, so free the memory it's using */
	HidD_FreePreparsedData(preparsedData);
	return 0;
}


/* ============================================================================== */
/* WINDOWS DDK HID SPECIFIC SUPPORT FUNCTIONS */
/* ============================================================================== */

short get_device_number_by_id(unsigned short vendor_id, unsigned short product_id)
{
	char path[MAX_PATH];
	char *pp = (char *)path;
	short ret, i;
	//short device_count = _hid_count_devices(); // mp20200205 doesn't set global
    device_count = _hid_count_devices(); // mp20200205 set global
	for (i = 0; i < device_count; i++)
	{
		/* get path for specified device number */
		ret = _hid_get_device_path(i, &pp, MAX_PATH);
		if (ret == -1)
		{
			return EXIT_FAILURE;
		}
		else
		{
			DWORD vid, pid;
				sscanf_s(pp, "\\\\?\\hid#vid_%04X&pid_%04X#", &vid, &pid); // grab the vid and pid of the device

			if(vid != vendor_id || pid != product_id)
			{
				continue;
			}

			return i;
		}
	}
	
	return EXIT_FAILURE;
}

short get_device_number_from_usage(short device_number, 
										unsigned short usage_page, 
										unsigned short usage)
{
	HANDLE fd = INVALID_HANDLE_VALUE;
	HIDP_CAPS capabilities;
	char path[MAX_PATH];
	char *pp = (char *)path;
	short ret, i;
	//short device_count = _hid_count_devices(); // mp20200205 doesn't set global
    device_count = _hid_count_devices(); // mp20200205 set global
	for (i = device_number; i < device_count; i++)
	{
		/* get path for specified device number */
		ret = _hid_get_device_path(i, &pp, MAX_PATH);
		if (ret == -1)
		{
			return EXIT_FAILURE;
		}
		else
		{
			/* open file on the device (read & write, no overlapp) */
			fd = CreateFile(path,
								 GENERIC_READ|GENERIC_WRITE,
								 FILE_SHARE_READ|FILE_SHARE_WRITE,
								 (LPSECURITY_ATTRIBUTES)NULL,
								 OPEN_EXISTING,
								 0,
								 NULL);
			if (fd == INVALID_HANDLE_VALUE)
			{
				return EXIT_FAILURE;
			}

			/* get the capabilities */
			_hid_get_capabilities(fd, &capabilities);

			/* check whether they match with what we want */
			if (capabilities.UsagePage == usage_page && capabilities.Usage == usage)
			{
				CloseHandle(fd);
				return i;
			}
			CloseHandle(fd);
		}
	}
	return EXIT_FAILURE;
}


/*
 * This function is needed to translate the USB HID relative flag into the
 * [hidio]/linux style events
 */
static void convert_axis_to_symbols(t_hid_element *new_element, int array_index)
{
	if (new_element->relative) 
	{ 
		new_element->type = ps_relative; 
		new_element->name = relative_symbols[array_index];
	}
	else 
	{ 
		new_element->type = ps_absolute; 
		new_element->name = absolute_symbols[array_index];
	}
}

static void get_usage_symbols(unsigned short usage_page, unsigned short usage, t_hid_element *new_element) 
{
	char buffer[MAXPDSTRING];

	debug_post(LOG_DEBUG, "get_usage_symbols for usage_page 0x%02X", usage_page);
	switch (usage_page)
	{
	case HID_USAGE_PAGE_GENERIC:
	    debug_post(LOG_DEBUG, "HID_USAGE_PAGE_GENERIC");
		switch (usage)
		{
			case HID_USAGE_GENERIC_X:
				debug_post(LOG_DEBUG, "HID_USAGE_GENERIC_X");
				convert_axis_to_symbols(new_element, 0);
				break;
			case HID_USAGE_GENERIC_Y: 
				debug_post(LOG_DEBUG, "HID_USAGE_GENERIC_Y");
				convert_axis_to_symbols(new_element, 1);
				break;
			case HID_USAGE_GENERIC_Z: 
				debug_post(LOG_DEBUG, "HID_USAGE_GENERIC_Z");
				convert_axis_to_symbols(new_element, 2);
				break;
			case HID_USAGE_GENERIC_RX: 
				debug_post(LOG_DEBUG, "HID_USAGE_GENERIC_RX");
				convert_axis_to_symbols(new_element, 3);
				break;
			case HID_USAGE_GENERIC_RY: 
				debug_post(LOG_DEBUG, "HID_USAGE_GENERIC_RY");
				convert_axis_to_symbols(new_element, 4);
				break;
			case HID_USAGE_GENERIC_RZ: 
				debug_post(LOG_DEBUG, "HID_USAGE_GENERIC_RZ");
				convert_axis_to_symbols(new_element, 5);
				break;
			case HID_USAGE_GENERIC_SLIDER: 
				debug_post(LOG_DEBUG, "HID_USAGE_GENERIC_SLIDER");
				convert_axis_to_symbols(new_element, 6);
				break;
			case HID_USAGE_GENERIC_DIAL: 
				debug_post(LOG_DEBUG, "HID_USAGE_GENERIC_DIAL");
				convert_axis_to_symbols(new_element, 7);
				break;
			case HID_USAGE_GENERIC_WHEEL: 
				debug_post(LOG_DEBUG, "HID_USAGE_GENERIC_WHEEL");
				convert_axis_to_symbols(new_element, 8);
				break; 
			case HID_USAGE_GENERIC_HATSWITCH: 
				debug_post(LOG_DEBUG, "HID_USAGE_GENERIC_HATSWITCH");
				// TODO: this is still a mystery how to handle, due to USB HID vs. Linux input.h
				new_element->type = ps_absolute; 
				new_element->name = absolute_symbols[9]; /* hatswitch */
				break;
			default:
				debug_post(LOG_DEBUG, "DESKTOP");
				new_element->type = gensym("DESKTOP");
				snprintf(buffer, sizeof(buffer), "DESKTOP%d", usage); 
				new_element->name = gensym(buffer);
		}
		break;
	case HID_USAGE_PAGE_SIMULATION:
	    debug_post(LOG_DEBUG, "HID_USAGE_PAGE_SIMULATION");
		switch (usage)
		{
		case HID_USAGE_SIMULATION_RUDDER: 
			debug_post(LOG_DEBUG, "HID_USAGE_SIMULATION_RUDDER");
			new_element->type = ps_absolute;
			new_element->name = absolute_symbols[5]; /* rz */
			break;
		case HID_USAGE_SIMULATION_THROTTLE:
			debug_post(LOG_DEBUG, "HID_USAGE_SIMULATION_THROTTLE");
			new_element->type = ps_absolute;
			new_element->name = absolute_symbols[6]; /* slider */
			break;
		default:
			debug_post(LOG_DEBUG, "SIMULATION");
			new_element->type = gensym("SIMULATION");
			snprintf(buffer, sizeof(buffer), "SIMULATION%d", usage); 
			new_element->name = gensym(buffer);
		}
		break;
	case HID_USAGE_PAGE_KEYBOARD:
	    debug_post(LOG_DEBUG, "HID_USAGE_PAGE_KEYBOARD");
		new_element->type = ps_key;
		if( (usage != 0xFFFF) && 
			(usage < KEY_ARRAY_MAX) )
			new_element->name = key_symbols[usage];
		else /* PowerBook ADB keyboard reports 0xffffffff */
			new_element->name = key_symbols[0];
		break;
	case HID_USAGE_PAGE_BUTTON:
	    debug_post(LOG_DEBUG, "HID_USAGE_PAGE_BUTTON");
		new_element->type = ps_button;
		new_element->name = button_symbols[usage];
		break;
	case HID_USAGE_PAGE_LED:
	    debug_post(LOG_DEBUG, "HID_USAGE_PAGE_LED");
		new_element->type = ps_led; 
		new_element->name = led_symbols[usage];
		break;
	case HID_USAGE_PAGE_DIGITIZER:	/* no sure whether this is the right for PID in OS X */
	    debug_post(LOG_DEBUG, "HID_USAGE_PAGE_DIGITIZER");
		new_element->type = ps_pid; 
		new_element->name = pid_symbols[usage];
		break;
	default:
	    debug_post(LOG_DEBUG, "HID_USAGE vendor defined");
		/* the rest are "vendor defined" so no translation table is possible */
		snprintf(buffer, sizeof(buffer), "0x%04x", (unsigned int) usage_page); 
		new_element->type = gensym(buffer); 
		snprintf(buffer, sizeof(buffer), "0x%04x", (unsigned int) usage); 
		new_element->name = gensym(buffer);
	}
}

static void hidio_build_element_list(t_hidio *x) 
{
	t_hid_device       *self = (t_hid_device *)x->x_hid_device;
	t_hid_element      *new_element = NULL;
	unsigned short     numButtons = 0;
	unsigned short     numValues = 0;
	unsigned short     numelem;	/* total number of elements */
    unsigned short     numCaps;
	unsigned long      i;
	short       	   bytes_required;
	short              bufSize;
    PHIDP_BUTTON_CAPS  buttonCaps = NULL;
    PHIDP_VALUE_CAPS   valueCaps = NULL;
    USAGE              usage;
	
	debug_post(LOG_DEBUG, "=*=hidio_build_element_list=*=");
	element_count[x->x_device_number] = 0;
	if (self->fh != INVALID_HANDLE_VALUE)
	{
	    debug_post(LOG_DEBUG, "hidio_build_element_list self->fh %d", self->fh);
		/* now get device capabilities */
		if(!HidD_GetPreparsedData(self->fh, &self->ppd))
		{
			pd_error (x, "HidD_GetPreparsedData error %lu", GetLastError());
		}
		if(HIDP_STATUS_INVALID_PREPARSED_DATA == HidP_GetCaps(self->ppd, &self->caps))
		{
			pd_error (x, "HidP_GetCaps error"); // if HidD_GetPreparsedData failed
		}

		/* allocate memory for input and output reports */
		/* these buffers get used for sending and receiving */
		/* some of them may be zero */
		self->inputReportBuffer = NULL;
        self->outputReportBuffer = NULL;
        self->featureReportBuffer = NULL;
        self->inputButtonCaps = buttonCaps = NULL;
        self->inputValueCaps = valueCaps = NULL;
        if (self->caps.InputReportByteLength)
		{
			bytes_required = (short)(self->caps.InputReportByteLength * sizeof(char));
     		self->inputReportBuffer = (bytes_required)?(char *)getbytes(bytes_required):NULL;
            debug_post(LOG_DEBUG, "inputReportBuffer size %d at %p", bytes_required, self->inputReportBuffer);
		}
		if (self->caps.OutputReportByteLength)
		{
			bytes_required = (short)(self->caps.OutputReportByteLength * sizeof(char));
    		self->outputReportBuffer = (bytes_required)?(char *)getbytes(bytes_required):NULL;
            debug_post(LOG_DEBUG, "outputReportBuffer size %d at %p", bytes_required, self->outputReportBuffer);
		}
        if (self->caps.FeatureReportByteLength)
		{
			bytes_required = (short)(self->caps.FeatureReportByteLength * sizeof(char));
    		self->featureReportBuffer = (bytes_required)?(char *)getbytes(bytes_required):NULL;
            debug_post(LOG_DEBUG, "featureReportBuffer size %d at %p", bytes_required, self->featureReportBuffer);
		}
		/* allocate memory for input info */
		if (self->caps.NumberInputButtonCaps)
		{
			bytes_required = (short)(self->caps.NumberInputButtonCaps * sizeof(HIDP_BUTTON_CAPS));
    		self->inputButtonCaps = buttonCaps = (bytes_required)?(PHIDP_BUTTON_CAPS)getbytes(bytes_required):NULL;
            debug_post(LOG_DEBUG, "inputButtonCaps size %d ast %p", bytes_required, self->inputButtonCaps);
		}
        if (self->caps.NumberInputValueCaps)
		{
			bytes_required = (short)(self->caps.NumberInputValueCaps * sizeof(HIDP_VALUE_CAPS));
    		self->inputValueCaps = valueCaps = (bytes_required)?(PHIDP_VALUE_CAPS)getbytes(bytes_required):NULL;
            debug_post(LOG_DEBUG, "NumberInputValueCaps size %d at %p", bytes_required, self->inputValueCaps);
		}


		/* get capapbilities for buttons and values from device */
		numCaps = self->caps.NumberInputButtonCaps;
        debug_post(LOG_DEBUG, "NumberInputButtonCaps %d", numCaps);
		if (numCaps) HidP_GetButtonCaps (HidP_Input, buttonCaps,	&numCaps, self->ppd);

		numCaps = self->caps.NumberInputValueCaps;
        debug_post(LOG_DEBUG, "NumberInputValueCaps %d", numCaps);
		if (numCaps) HidP_GetValueCaps (HidP_Input, valueCaps, &numCaps, self->ppd);

		/* number of elements is number of values (axes) plus number of buttons */
#if 1
		for (i = 0; i < self->caps.NumberInputValueCaps; i++, valueCaps++) 
		{
			if (valueCaps->IsRange) 
				numValues += valueCaps->Range.UsageMax - valueCaps->Range.UsageMin + 1;
			else
				numValues++;
		}
		for (i = 0; i < self->caps.NumberInputButtonCaps; i++, buttonCaps++) 
		{
			if (buttonCaps->IsRange) 
				numButtons += buttonCaps->Range.UsageMax - buttonCaps->Range.UsageMin + 1;
			else
				numButtons++;
		}
		numelem = numValues + numButtons;
        debug_post(LOG_DEBUG, "numValues %d numButtons %d numelem %d", numValues, numButtons, numelem);

		valueCaps = self->inputValueCaps;
		buttonCaps = self->inputButtonCaps;
#else
		/* maybe this could be done as well? works with my gamepad at least */
		numelem = HidP_MaxDataListLength(HidP_Input, self->ppd);
				   + HidP_MaxUsageListLength(HidP_Input, 0, self->ppd);
#endif

        /* now look through the reported capabilities of the device and fill in the elements struct */
		debug_post(LOG_DEBUG, "===Getting %d buttonCaps===", self->caps.NumberInputButtonCaps);
    	bytes_required = sizeof(t_hid_element);
		for (i = 0; i < self->caps.NumberInputButtonCaps; i++, buttonCaps++) 
		{
			debug_post(LOG_DEBUG, ".buttonCaps %p UsagePage:0x%02X IsRange:%d", buttonCaps, buttonCaps->UsagePage, (buttonCaps->IsRange)?1:0);
			if (buttonCaps->IsRange) 
			{
     			debug_post(LOG_DEBUG, "..Range.UsageMin %d UsageMax %d", buttonCaps->Range.UsageMin, buttonCaps->Range.UsageMax);
				for (usage = buttonCaps->Range.UsageMin; usage <= buttonCaps->Range.UsageMax; usage++)
				{
//					bytes_required = sizeof(t_hid_element);
					new_element = getbytes(bytes_required);
					new_element->usage_page = buttonCaps->UsagePage;
					new_element->usage_id = usage;
        			debug_post(LOG_DEBUG, "...new_element %p(%d bytes) usage_page 0x%02X usage_id %d", new_element, bytes_required, new_element->usage_page, new_element->usage_id);
					new_element->relative = !buttonCaps->IsAbsolute;	/* buttons always are absolute, no? */
					new_element->min = 0;
					new_element->max = 1;
					new_element->instance = 0;

					get_usage_symbols(new_element->usage_page, new_element->usage_id, new_element);
#ifdef PD
					SETSYMBOL(new_element->output_message, new_element->name); 
					SETFLOAT(new_element->output_message + 1, new_element->instance);
#else /* Max */
					atom_setsym(new_element->output_message, new_element->name);
					atom_setlong(new_element->output_message + 1, (long)new_element->instance);
#endif /* PD */
       			    debug_post(LOG_DEBUG, "...new_element->name %s, new_element->instance %d", new_element->name->s_name, new_element->instance);
					element[x->x_device_number][element_count[x->x_device_number]] = new_element;
					++element_count[x->x_device_number];
				}
			}
			else
			{
//    			bytes_required = sizeof(t_hid_element);
				new_element = getbytes(bytes_required);
				new_element->usage_page = buttonCaps->UsagePage;
				new_element->usage_id = buttonCaps->NotRange.Usage;
       			debug_post(LOG_DEBUG, "..single new_element %p(%d bytes) usage_page 0x%02X usage_id %d", new_element, bytes_required, new_element->usage_page, new_element->usage_id);
				new_element->relative = !buttonCaps->IsAbsolute;	/* buttons always are absolute, no? */
				new_element->min = 0;
				new_element->max = 1;
				new_element->instance = 0;

				get_usage_symbols(new_element->usage_page, new_element->usage_id, new_element);
#ifdef PD
				SETSYMBOL(new_element->output_message, new_element->name); 
				SETFLOAT(new_element->output_message + 1, new_element->instance);
#else /* Max */
				atom_setsym(new_element->output_message, new_element->name);
				atom_setlong(new_element->output_message + 1, (long)new_element->instance);
#endif /* PD */
   			    debug_post(LOG_DEBUG, "..new_element->name %s, new_element->instance %d", new_element->name->s_name, new_element->instance);
				element[x->x_device_number][element_count[x->x_device_number]] = new_element;
				++element_count[x->x_device_number];
			}
   			debug_post(LOG_DEBUG, ".element_count[%d]: %d", x->x_device_number, element_count[x->x_device_number]);
		}
		/* get value data */
		debug_post(LOG_DEBUG, "===Getting %d valueCaps===", self->caps.NumberInputValueCaps);
		for (i = 0; i < self->caps.NumberInputValueCaps; i++, valueCaps++)
		{
            debug_post(LOG_DEBUG, ".valueCaps %p usage_page 0x%02X, IsRange: %d", valueCaps, valueCaps->UsagePage, valueCaps->IsRange);
			if (valueCaps->IsRange) 
			{
     			debug_post(LOG_DEBUG, "..Range.UsageMin %d UsageMax %d", valueCaps->Range.UsageMin, valueCaps->Range.UsageMax);
				for (usage = valueCaps->Range.UsageMin; usage <= valueCaps->Range.UsageMax; usage++) 
				{
//					bytes_required = sizeof(t_hid_element);
					new_element = getbytes(bytes_required);
					new_element->usage_page = valueCaps->UsagePage;
					new_element->usage_id = usage;
        			debug_post(LOG_DEBUG, "...new_element %p(%d bytes) usage_page 0x%02X usage_id %d", new_element, bytes_required, new_element->usage_page, new_element->usage_id);
					new_element->relative = !valueCaps->IsAbsolute;
					new_element->min = valueCaps->LogicalMin;
					new_element->max = valueCaps->LogicalMax;
					new_element->instance = 0;
					
					get_usage_symbols(new_element->usage_page, new_element->usage_id, new_element);
#ifdef PD
					SETSYMBOL(new_element->output_message, new_element->name); 
					SETFLOAT(new_element->output_message + 1, new_element->instance);
#else /* Max */
					atom_setsym(new_element->output_message, new_element->name);
					atom_setlong(new_element->output_message + 1, (long)new_element->instance);
#endif /* PD */
					element[x->x_device_number][element_count[x->x_device_number]] = new_element;
     			    debug_post(LOG_DEBUG, "...new_element->name %s, new_element->instance %d", new_element->name->s_name, new_element->instance);
					++element_count[x->x_device_number];
				}
			} 
			else
			{
//				bytes_required = sizeof(t_hid_element);
				new_element = getbytes(bytes_required);
				new_element->usage_page = valueCaps->UsagePage;
				new_element->usage_id = valueCaps->NotRange.Usage;
       			debug_post(LOG_DEBUG, "..single new_element %p(%d bytes) usage_page 0x%02X usage_id %d", new_element, bytes_required, new_element->usage_page, new_element->usage_id);
				new_element->relative = !valueCaps->IsAbsolute;
				new_element->min = valueCaps->LogicalMin;
				new_element->max = valueCaps->LogicalMax;
				new_element->instance = 0;
				
				get_usage_symbols(new_element->usage_page, new_element->usage_id, new_element);
#ifdef PD
				SETSYMBOL(new_element->output_message, new_element->name); 
				SETFLOAT(new_element->output_message + 1, new_element->instance);
#else /* Max */
				atom_setsym(new_element->output_message, new_element->name);
				atom_setlong(new_element->output_message + 1, (long)new_element->instance);
#endif /* PD */
				element[x->x_device_number][element_count[x->x_device_number]] = new_element;
     			    debug_post(LOG_DEBUG, "..new_element->name %s, new_element->instance %d", new_element->name->s_name, new_element->instance);
				++element_count[x->x_device_number];
			}
   			debug_post(LOG_DEBUG, ".element_count[%d]: %d", x->x_device_number, element_count[x->x_device_number]);
		}
	}
    debug_post(LOG_DEBUG, "=*=hidio_build_element_list done.=*=");
}

t_int hidio_print_element_list(t_hidio *x)
{
	//t_hid_device *self = (t_hid_device *)x->x_hid_device;
	t_hid_element *current_element;
	int i;

	debug_post(LOG_DEBUG,"hidio_print_element_list");

	post("[hidio] found %d elements:", element_count[x->x_device_number]);
	post("\nTYPE\tCODE#\tEVENT NAME\t\tmin-max");
	post("--------------------------------------------------------------------");
	for (i = 0; i < element_count[x->x_device_number]; i++)
	{
		current_element = element[x->x_device_number][i];
		post("  %s\t%d\t%s\t\t%d-%d", current_element->type->s_name,
			 current_element->usage_id, current_element->name->s_name,
			 current_element->min, current_element->max, current_element->value);
	}
	post("");

	return EXIT_SUCCESS;	
}

t_int hidio_print_device_list(t_hidio *x) 
{
	//t_hid_device                    *self = (t_hid_device *)x->x_hid_device;
	struct _GUID                    GUID;
	SP_INTERFACE_DEVICE_DATA        DeviceInterfaceData;
	struct
	{
		DWORD cbSize;
		char DevicePath[MAX_PATH];
	}                               FunctionClassDeviceData;
	HIDD_ATTRIBUTES                 HIDAttributes;
	SECURITY_ATTRIBUTES             SecurityAttributes;
	int                             i;
	HANDLE                          PnPHandle, HIDHandle;
	ULONG                           BytesReturned;
	BOOLEAN                             Success, haveManufacturerName, haveProductName;
	PWCHAR                          widestring[MAXPDSTRING];
	char                            ManufacturerBuffer[MAXPDSTRING];
	char                            ProductBuffer[MAXPDSTRING];
	const char                      NotSupplied[] = "NULL";
	DWORD                           lastError = 0;

	/* Initialize the GUID array and setup the security attributes for Win2000 */
	HidD_GetHidGuid(&GUID);
	SecurityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
	SecurityAttributes.lpSecurityDescriptor = NULL;
	SecurityAttributes.bInheritHandle = FALSE;

	/* Get a handle for the Plug and Play node and request currently active devices */
	PnPHandle = SetupDiGetClassDevs(&GUID, NULL, NULL, DIGCF_PRESENT|DIGCF_INTERFACEDEVICE);

	if (PnPHandle == INVALID_HANDLE_VALUE) 
	{ 
		error("[hidio] ERROR: Could not attach to PnP node");
		return (t_int) GetLastError();
	}

	post("\n[hidio]: current device list:");

	/* Lets look for a maximum of 32 Devices */
	for (i = 0; i < MAX_DEVICES; i++)
	{
		/* Initialize our data */
		DeviceInterfaceData.cbSize = sizeof(DeviceInterfaceData);
		/* Is there a device at this table entry */
		Success = SetupDiEnumDeviceInterfaces(PnPHandle, NULL, &GUID, i, &DeviceInterfaceData);
		if (Success)
		{
			/* There is a device here, get its name */
			FunctionClassDeviceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
			Success = SetupDiGetDeviceInterfaceDetail(PnPHandle, 
					&DeviceInterfaceData, NULL, 0, &BytesReturned, NULL);
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) 
			{ 
                /* first call returned size of data, now call it again with the required size */
				if (global_debug_level >= LOG_DEBUG) post("[hidio] need %lu bytes for data", BytesReturned); 
    			FunctionClassDeviceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
    			Success = SetupDiGetDeviceInterfaceDetail(PnPHandle, 
					&DeviceInterfaceData,
					(PSP_INTERFACE_DEVICE_DETAIL_DATA)&FunctionClassDeviceData, 
					BytesReturned, &BytesReturned, NULL);

     			if (!Success) 
	    		{ 
		    		error("[hidio] ERROR: Could not find the system name for device %d",i); 
			    	return GetLastError();
			    }
		    }
			/* Can now open this device */
			HIDHandle = CreateFile(FunctionClassDeviceData.DevicePath, 
				0, FILE_SHARE_READ|FILE_SHARE_WRITE, &SecurityAttributes,
				OPEN_EXISTING, 0, NULL);
			lastError =  GetLastError();
			if (HIDHandle == INVALID_HANDLE_VALUE) 
			{
				error("[hidio] ERROR: Could not open HID #%d, Errorcode = %d", i, (int)lastError);
				return lastError;
			}
			
			/* Get the information about this HID */
			Success = HidD_GetAttributes(HIDHandle, &HIDAttributes);
			if (!Success) 
			{ 
				error("[hidio] ERROR: Could not get HID attributes"); 
				return GetLastError(); 
			}
			haveManufacturerName = HidD_GetManufacturerString(HIDHandle, widestring, MAXPDSTRING);
			wcstombs(ManufacturerBuffer, (const unsigned short *)widestring, MAXPDSTRING);
			haveProductName = HidD_GetProductString(HIDHandle, widestring, MAXPDSTRING);
			wcstombs(ProductBuffer, (const unsigned short *)widestring, MAXPDSTRING);

			/* And display it! */
			post("__________________________________________________");
			post("Device %d: '%s' '%s' version %d", i, 
				 haveManufacturerName ? ManufacturerBuffer : NotSupplied, haveProductName ? ProductBuffer : NotSupplied, 
				 HIDAttributes.VersionNumber);
			post("    vendorID: 0x%04x    productID: 0x%04x",
				 HIDAttributes.VendorID, HIDAttributes.ProductID);

			CloseHandle(HIDHandle);
		} // if (SetupDiEnumDeviceInterfaces . .
	} // for (i = 0; i < 32; i++)
	SetupDiDestroyDeviceInfoList(PnPHandle);

	post("");

	return EXIT_SUCCESS;
}

void hidio_output_device_name(t_hidio *x, char *manufacturer, char *product) 
{
	//t_hid_device *self = (t_hid_device *)x->x_hid_device;
	char      *device_name;
	t_symbol  *device_name_symbol;

	device_name = malloc( strlen(manufacturer) + 1 + strlen(product) + 1 );
//	device_name = malloc( 7 + strlen(manufacturer) + 1 + strlen(product) + 1 );
//	strcpy( device_name, "append " );
	strcat( device_name, manufacturer );
	strcat ( device_name, " ");
	strcat( device_name, product );
//	outlet_anything( x->x_status_outlet, gensym( device_name ),0,NULL );
#ifdef PD
	outlet_symbol( x->x_status_outlet, gensym( device_name ) );
#else
	outlet_anything( x->x_status_outlet, gensym( device_name ),0,NULL );
#endif
}

/* ------------------------------------------------------------------------------ */
/*  FORCE FEEDBACK FUNCTIONS */
/* ------------------------------------------------------------------------------ */

/* cross-platform force feedback functions */
t_int hidio_ff_autocenter( t_hidio *x, t_float value )
{
	//t_hid_device *self = (t_hid_device *)x->x_hid_device;
	return EXIT_SUCCESS;
}


t_int hidio_ff_gain( t_hidio *x, t_float value )
{
	//t_hid_device *self = (t_hid_device *)x->x_hid_device;
	return EXIT_SUCCESS;
}


t_int hidio_ff_motors( t_hidio *x, t_float value )
{
	//t_hid_device *self = (t_hid_device *)x->x_hid_device;
	return EXIT_SUCCESS;
}


t_int hidio_ff_continue( t_hidio *x )
{
	//t_hid_device *self = (t_hid_device *)x->x_hid_device;
	return EXIT_SUCCESS;
}


t_int hidio_ff_pause( t_hidio *x )
{
	//t_hid_device *self = (t_hid_device *)x->x_hid_device;
	return EXIT_SUCCESS;
}


t_int hidio_ff_reset( t_hidio *x )
{
	//t_hid_device *self = (t_hid_device *)x->x_hid_device;
	return EXIT_SUCCESS;
}


t_int hidio_ff_stopall( t_hidio *x )
{
	//t_hid_device *self = (t_hid_device *)x->x_hid_device;
	return EXIT_SUCCESS;
}



// these are just for testing...
t_int hidio_ff_fftest ( t_hidio *x, t_float value)
{
	//t_hid_device *self = (t_hid_device *)x->x_hid_device;
	return EXIT_SUCCESS;
}


/* ============================================================================== */
/* Pd [hidio] FUNCTIONS */
/* ============================================================================== */

void hidio_elements(t_hidio *x)
{
	if (x->x_device_open) hidio_print_element_list(x);
}

void hidio_write_event_symbols(t_hidio *x, t_symbol *type, t_symbol *name, t_int instance, t_int value)
{
	post("hidio_write_event_symbols needs coding");
}

void hidio_write_event_ints(t_hidio *x, t_int type, t_int name, t_int instance, t_int value)
{
	post("hidio_write_event_ints needs coding");
}

void hidio_write_event_symbol_int(t_hidio *x, t_symbol *type, t_int code, t_int instance, t_int value)
{
	post("hidio_write_event_symbol_int needs coding");
}

void hidio_devices(t_hidio *x)
{
	hidio_print_device_list(x);
}

void hidio_platform_specific_info(t_hidio *x)
{
	t_hid_device                    *self = (t_hid_device *)x->x_hid_device;
	HIDD_ATTRIBUTES                 HIDAttributes;
	int                             devNr;
	HANDLE                          HIDHandle;
	BOOLEAN                         haveManufacturerName, haveProductName;
	PWCHAR                          widestring[MAXPDSTRING];
	char                            ManufacturerBuffer[MAXPDSTRING];
	char                            ProductBuffer[MAXPDSTRING];
    char                            SerialNumberBuffer[MAXPDSTRING];
    char                            vendor_id_string[8];
    char                            product_id_string[8];
    char                            version_string[8];
    char                            device_type_buffer[8];
    const char                      NotSupplied[] = "NULL";
    t_hid_element                   *current_element;
    t_symbol                        *output_symbol;
    t_atom                          *output_atom = (t_atom *)getbytes(sizeof(t_atom));

	debug_post(LOG_DEBUG,"hidio_platform_specific_info");
	/* Get info for the currently open device */
	if ((( devNr = x->x_device_number) >= 0) && (self->fh != INVALID_HANDLE_VALUE))
	{
        HIDHandle = self->fh;
		/* Get the information about this HID */
		if (!HidD_GetAttributes(HIDHandle, &HIDAttributes)) 
		{ 
			pd_error(x, "[hidio] ERROR: Could not get HID attributes (%lu)", GetLastError()); 
			return;
		}
		haveManufacturerName = HidD_GetManufacturerString(HIDHandle, widestring, MAXPDSTRING);
		wcstombs(ManufacturerBuffer, (const unsigned short *)widestring, MAXPDSTRING);
		haveProductName = HidD_GetProductString(HIDHandle, widestring, MAXPDSTRING);
		wcstombs(ProductBuffer, (const unsigned short *)widestring, MAXPDSTRING);
		/*product manufacturer transport type vendorID productID*/
        /* product */
		SETSYMBOL(output_atom, gensym(haveProductName? ProductBuffer: NotSupplied));
		outlet_anything( x->x_status_outlet, gensym("product"), 1, output_atom);
		/* manufacturer */
		SETSYMBOL(output_atom, gensym(haveManufacturerName? ManufacturerBuffer: NotSupplied));
		outlet_anything( x->x_status_outlet, gensym("manufacturer"), 1, output_atom);
		/* serial number */
		if(HidD_GetSerialNumberString(HIDHandle, widestring, MAXPDSTRING))
		{
            wcstombs(SerialNumberBuffer, (const unsigned short *)widestring, MAXPDSTRING);
            output_symbol = gensym(SerialNumberBuffer);
#ifdef PD
			if( output_symbol != &s_ )
#else
			if( output_symbol != _sym_nothing )
#endif
			{ /* the serial is rarely used on USB devices, so test for it */
				SETSYMBOL(output_atom, output_symbol);
				outlet_anything( x->x_status_outlet, gensym("serial"), 1, output_atom);
			}
        }
        /* transport, it's usually USB, no? */
        /* vendor id */
		sprintf(vendor_id_string,"0x%04x", HIDAttributes.VendorID);
		SETSYMBOL(output_atom, gensym(vendor_id_string));
		outlet_anything( x->x_status_outlet, gensym("vendorID"), 1, output_atom);
        /* product id */
		sprintf(product_id_string,"0x%04x", HIDAttributes.ProductID);
		SETSYMBOL(output_atom, gensym(product_id_string));
		outlet_anything( x->x_status_outlet, gensym("productID"), 1, output_atom);
        /* version */
		sprintf(version_string,"0x%04x", HIDAttributes.VersionNumber);
		SETSYMBOL(output_atom, gensym(version_string));
		outlet_anything( x->x_status_outlet, gensym("version"), 1, output_atom);
        /* type (the usage page?) */
        current_element = element[devNr][0];
        sprintf(device_type_buffer,"0x%04x", current_element->usage_page);
		SETSYMBOL(output_atom, gensym(device_type_buffer));
		outlet_anything( x->x_status_outlet, gensym("type"), 1, output_atom);
	} // if (( devNr = x->x_device_number) >= 0)
	freebytes(output_atom,sizeof(t_atom));
	debug_post(LOG_DEBUG,"end hidio_platform_specific_info");
}

void hidio_get_events(t_hidio *x)
{
	t_hid_device *self = (t_hid_device *)x->x_hid_device;
	t_hid_element *current_element = NULL;
	long bytesRead;
	int devNr = x->x_device_number;
    debug_post(9,"hidio_get_events");
	while ((bytesRead = _hidio_read(self)) > 0)
	{
		unsigned long i;
		unsigned long size, length;
		unsigned short *usages;
		NTSTATUS result;

       	debug_post(LOG_DEBUG,"hidio_get_events device %d (%d elements) got an event (%lu bytes):", devNr, element_count[devNr], bytesRead);
		for (i = 0; i < element_count[devNr]; i++)
		{
			current_element = element[devNr][i];

			/* first try getting value data */
         	debug_post(LOG_DEBUG,"HidP_GetUsageValue for current_element[%d](at %p) usage_page 0x%02X, usage_id %d", i, current_element, current_element->usage_page, current_element->usage_id);
			result = HidP_GetUsageValue(HidP_Input, current_element->usage_page, 0, current_element->usage_id,
    			(unsigned long *)&current_element->value, 
				self->ppd, self->inputReportBuffer, self->caps.InputReportByteLength);
			switch (result)
			{
				case HIDP_STATUS_SUCCESS:
					debug_post(LOG_DEBUG,"HidP_GetUsageValue: The routine successfully returned the value data.");
					break;
				case HIDP_STATUS_INVALID_REPORT_LENGTH:
					debug_post(LOG_DEBUG,"HidP_GetUsageValue: The report length is not valid.");
					break;
				case HIDP_STATUS_INVALID_REPORT_TYPE:
					debug_post(LOG_DEBUG,"HidP_GetUsageValue: The specified report type is not valid.");
					break;
				case HIDP_STATUS_INCOMPATIBLE_REPORT_ID:
					debug_post(LOG_DEBUG,"HidP_GetUsageValue: The collection contains a value on the specified usage page in a report of the specified type, but there are no such usages in the specified report.");
					break;
				case HIDP_STATUS_INVALID_PREPARSED_DATA:
					debug_post(LOG_DEBUG,"HidP_GetUsageValue: The preparsed data is not valid.");
					break;
				case HIDP_STATUS_USAGE_NOT_FOUND:
					debug_post(LOG_DEBUG,"HidP_GetUsageValue: The collection does not contain a value on the specified usage page in any report of the specified report type.");
					break;					
				default:
					debug_post(LOG_DEBUG,"HidP_GetUsageValue: No idea.");
			}
			if (HIDP_STATUS_SUCCESS == result)
			{
            	debug_post(LOG_DEBUG,"***HidP_GetUsageValue %d", current_element->value);
				continue;
			}
			/* now try getting scaled value data */
         	debug_post(LOG_DEBUG,"HidP_GetScaledUsageValue for element %d (at %p) usage_page 0x%02X, usage_id %d", i, current_element, current_element->usage_page, current_element->usage_id);
			result = HidP_GetScaledUsageValue(HidP_Input, current_element->usage_page, 0, current_element->usage_id, &current_element->value, 
										self->ppd, self->inputReportBuffer, self->caps.InputReportByteLength);
			switch (result)
			{
            	case HIDP_STATUS_SUCCESS:
					debug_post(LOG_DEBUG,"HidP_GetScaledUsageValue: The routine successfully returned the value data.");
					break;
				case HIDP_STATUS_INVALID_REPORT_LENGTH:
					debug_post(LOG_DEBUG,"HidP_GetScaledUsageValue: The report length is not valid.");
					break;
				case HIDP_STATUS_INVALID_REPORT_TYPE:
					debug_post(LOG_DEBUG,"HidP_GetScaledUsageValue: The specified report type is not valid.");
					break;
				case HIDP_STATUS_BAD_LOG_PHY_VALUES:
					debug_post(LOG_DEBUG,"HidP_GetScaledUsageValue: The collection returned an illegal logical or physical value. To extract the value returned by the collection, call HidP_GetUsageValue.");
					break;
				case HIDP_STATUS_INCOMPATIBLE_REPORT_ID:
					debug_post(LOG_DEBUG,"HidP_GetScaledUsageValue: The specified value is not contained in the specified report, but is contained in another report supported by the specified top-level collection.");
					break;
				case HIDP_STATUS_NULL:
					debug_post(LOG_DEBUG, "HidP_GetScaledUsageValue: The current state of the scaled value from the collection is less than the logical minimum or is greater than the logical maximum, and the scaled value has a NULL state.");
					break;
				case HIDP_STATUS_VALUE_OUT_OF_RANGE:
					debug_post(LOG_DEBUG, "HidP_GetScaledUsageValue: The current state of the scaled value data from the collection is less than the logical minimum or is greater than the logical maximum.");
					break;
				case HIDP_STATUS_USAGE_NOT_FOUND:
					debug_post(LOG_DEBUG, "HidP_GetScaledUsageValue: The specified usage, usage page, or link collection cannot be found in any report supported by the specified top-level collection.");
					break;
				default:
					debug_post(LOG_DEBUG,"HidP_GetScaledUsageValue: No idea.");
			}
			if (HIDP_STATUS_SUCCESS == result)
			{
            	debug_post(LOG_DEBUG,"***HidP_GetScaledUsageValue %d", current_element->value);
				continue;
			}

			/* ask Windows how many usages we might expect at max. */
			length = size = HidP_MaxUsageListLength(HidP_Input, current_element->usage_page, self->ppd);
            debug_post(LOG_DEBUG,"HidP_MaxUsageListLength for element %d is %d bytes", i, length);
			if (size)
			{
            	debug_post(LOG_DEBUG,"HidP_MaxUsageListLength is %d", size);
				/* uh, can't I alloc this memory in advance? but we might have several button usages on
				   different usage pages, so I'd have to figure out the largest one */
				usages = (unsigned short *)getbytes((short)(size * sizeof(unsigned short)));
              	if (usages == NULL)
				{
					debug_post(LOG_DEBUG,"getbytes failed.");
				    continue;
				}
				/* finally try getting button data */
				if (HIDP_STATUS_SUCCESS == HidP_GetUsages(HidP_Input, current_element->usage_page, 0, usages, &length, 
					self->ppd, self->inputReportBuffer, self->caps.InputReportByteLength))
				{
					unsigned long j;

                	debug_post(LOG_DEBUG,"HidP_GetUsages element %d usage_id %d", i, current_element->usage_id);
                	debug_post(LOG_DEBUG,"self->inputReportBuffer %p self->caps.InputReportByteLength %d", self->inputReportBuffer, self->caps.InputReportByteLength);
					current_element->value = 0;
					// length is set to the number of buttons that are set to ON on the specified usage page
                	debug_post(LOG_DEBUG,"length = %d (buttons that are ON in this usage page 0x%02X), usages[0]= %d", length, current_element->usage_page, usages[0]);

					for (j = 0; ((j < length)&&(usages[j] != 0)); j++)
					{
                      	debug_post(LOG_DEBUG,"***HidP_GetUsages element %d usage_id %d, usages[%d]=%d",
    						i, current_element->usage_id, j, usages[j]);
						if (current_element->usage_id == usages[j])
						{
                        	debug_post(LOG_DEBUG,"*** HidP_GetUsages element %d", i);
							current_element->value = 1;
							break;
						}
					}
				}
				freebytes(usages, (short)(size * sizeof(unsigned short)));
			}
		}
	}
}


t_int hidio_open_device(t_hidio *x, short device_number)
{
	t_hid_device *self = (t_hid_device *)x->x_hid_device;

	if (device_number >= 0)
	{
		// open new device
		self->fh = connectDeviceNumber(device_number);

		if (self->fh != INVALID_HANDLE_VALUE)
		{
			// set device_number before calling hidio_build_element_list
			x->x_device_number = device_number;
			hidio_build_element_list(x);

			/* prepare overlapped structure */
			self->overlapped.Offset     = 0; 
			self->overlapped.OffsetHigh = 0; 
			self->overlapped.hEvent     = CreateEvent(NULL, TRUE, FALSE, "");
 
			return EXIT_SUCCESS;
		}
		else
			return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


t_int hidio_close_device(t_hidio *x)
{
	t_hid_device *self = (t_hid_device *)x->x_hid_device;

	debug_post(LOG_DEBUG, "hidio_close_device");	
	if (x->x_device_number > -1)
	{
		if ((self->fh != INVALID_HANDLE_VALUE) && (x->x_device_open != 0))
		{
			unsigned long i;
			CloseHandle(self->fh);
			self->fh = INVALID_HANDLE_VALUE;

			/* free element list */
			for (i = 0; i < element_count[x->x_device_number]; i++)
			{
				freebytes(element[x->x_device_number][i], sizeof(t_hid_element));
				element[x->x_device_number][i] = INVALID_HANDLE_VALUE;
			}
            
			element_count[x->x_device_number] = 0;

			/* free allocated memory */
			if (self->inputButtonCaps)
				freebytes(self->inputButtonCaps, (short)(self->caps.NumberInputButtonCaps * sizeof(HIDP_BUTTON_CAPS)));
			if (self->inputValueCaps)
				freebytes(self->inputValueCaps, (short)(self->caps.NumberInputValueCaps * sizeof(HIDP_VALUE_CAPS)));
			/* free report buffers */
			if (self->inputReportBuffer)
				freebytes(self->inputReportBuffer, (short)(self->caps.InputReportByteLength * sizeof(char)));
			if (self->outputReportBuffer)
				freebytes(self->outputReportBuffer, (short)(self->caps.OutputReportByteLength * sizeof(char)));
			if (self->featureReportBuffer)
				freebytes(self->featureReportBuffer, (short)(self->caps.FeatureReportByteLength * sizeof(char)));
			/* free preparsed data */
			if (self->ppd)
				HidD_FreePreparsedData(self->ppd);
		}
	}
	return EXIT_SUCCESS;
}

void hidio_build_device_list(void)
{
	debug_post(LOG_DEBUG,"hidio_build_device_list");
}

void hidio_print(t_hidio *x)
{
	int result;
	//t_hid_device *self = (t_hid_device *)x->x_hid_device;

	result = hidio_print_device_list(x);
	post("hidio_print_device_list returned %d", result);
	
	if (x->x_device_open) hidio_print_element_list(x);
}


void hidio_platform_specific_free(t_hidio *x)
{
	t_hid_device *self = (t_hid_device *)x->x_hid_device;

	debug_post(LOG_DEBUG,"hidio_platform_specific_free");

	if (self)
		freebytes(self, sizeof(t_hid_device));
}


void *hidio_platform_specific_new(t_hidio *x)
{
	t_hid_device *self;

	debug_post(LOG_DEBUG,"hidio_platform_specific_new");

	/* alloc memory for our instance */
	self = (t_hid_device *)getbytes(sizeof(t_hid_device));
	self->fh = INVALID_HANDLE_VALUE;

	return (void *)self;	/* return void pointer to our data struct */
}

#endif  /* _WIN32 */
