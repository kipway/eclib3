/*!
\file ec_netmac.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.8.29

eclib get the network card MAC address for windows and linux

class CMacAddr;

eclib 3.0 Copyright (c) 2017-2018, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

简介：
获取网卡mac地址的函数 getnetmac

*/
#pragma once

#ifdef _WIN32
#include <windows.h>
#include <NtDDNdis.h>
#include <winioctl.h>
#pragma comment (lib, "setupapi.lib")
#include <tchar.h>
#include <strsafe.h>
#include <setupapi.h>
#include <ntddndis.h>
#include <algorithm>
#else
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <net/if_arp.h>
#ifdef SOLARIS
#include <sys/sockio.h>
#endif
#endif

namespace ec
{
#ifdef _WIN32    
    static const GUID GUID_QUERYSET[] = {
        { 0xAD498944, 0x762F, 0x11D0, 0x8D, 0xCB, 0x00, 0xC0, 0x4F, 0xC3, 0x35, 0x8C },//native MAC include usb net card        
        { 0xAD498944, 0x762F, 0x11D0, 0x8D, 0xCB, 0x00, 0xC0, 0x4F, 0xC3, 0x35, 0x8C },//native MAC exclude usb net card
    };

    class CMacAddr
    {
    public:
        typedef struct _T_MACADDRESS {
            BYTE	PermanentAddress[6];	//native MAC address
            BYTE	MACAddress[6];			//current MAC address
        } T_MACADDRESS;
    public:
        CMacAddr() {};
        ~CMacAddr() {};
    public:
        static int GetNetMAC(unsigned char ucbuf[48]) // ret MAC NUMS
        {
            T_MACADDRESS netinfo[8];
            int i, n = WDK_MacAddress(0, netinfo, 8); // MAC
            for (i = 0; i < n; i++)
                memcpy(&ucbuf[i * 6], netinfo[i].PermanentAddress, 6);
            return n;
        };
    protected:
        static BOOL WDK_GetMacAddress(TCHAR* DevicePath, T_MACADDRESS *pMacAddress, INT iIndex, BOOL isIncludeUSB)
        {
            HANDLE	hDeviceFile;
            BOOL	isOK = FALSE;
            if (_tcsnicmp(DevicePath + 4, TEXT("root"), 4) == 0)//excluse virtual net
                return FALSE;
            if (!isIncludeUSB)
            {
                if (_tcsnicmp(DevicePath + 4, TEXT("usb"), 4) == 0)//excluse usb net
                    return FALSE;
            }
            hDeviceFile = CreateFile(DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (hDeviceFile != INVALID_HANDLE_VALUE)
            {
                ULONG	dwID;
                BYTE	ucData[8];
                DWORD	dwByteRet;

                dwID = OID_802_3_CURRENT_ADDRESS;
                isOK = DeviceIoControl(hDeviceFile, IOCTL_NDIS_QUERY_GLOBAL_STATS, &dwID, sizeof(dwID), ucData, sizeof(ucData), &dwByteRet, NULL);
                if (isOK)
                {
                    memcpy(pMacAddress[iIndex].MACAddress, ucData, dwByteRet);
                    dwID = OID_802_3_PERMANENT_ADDRESS;
                    isOK = DeviceIoControl(hDeviceFile, IOCTL_NDIS_QUERY_GLOBAL_STATS, &dwID, sizeof(dwID), ucData, sizeof(ucData), &dwByteRet, NULL);
                    if (isOK)
                        memcpy(pMacAddress[iIndex].PermanentAddress, ucData, dwByteRet);
                }
                CloseHandle(hDeviceFile);
            }
            return isOK;
        }

        static BOOL WDK_GetProperty(TCHAR* DevicePath, INT iQueryType, T_MACADDRESS *pMacAddress, INT iIndex)
        {
            BOOL isOK = FALSE;

            switch (iQueryType)
            {
            case 0:
                isOK = WDK_GetMacAddress(DevicePath, pMacAddress, iIndex, TRUE);
                break;
            case 1:
                isOK = WDK_GetMacAddress(DevicePath, pMacAddress, iIndex, FALSE);
                break;
            default:
                break;
            }
            return isOK;
        }

        static INT WDK_MacAddress(INT iQueryType, T_MACADDRESS *pMacAddress, INT iSize)
        {
            HDEVINFO	hDevInfo;
            DWORD		MemberIndex, RequiredSize;
            SP_DEVICE_INTERFACE_DATA			DeviceInterfaceData;
            PSP_DEVICE_INTERFACE_DETAIL_DATA	DeviceInterfaceDetailData;
            INT	iTotal = 0;

            if ((iQueryType < 0) || (iQueryType >= sizeof(GUID_QUERYSET) / sizeof(GUID)))
                return -2;

            hDevInfo = SetupDiGetClassDevs(GUID_QUERYSET + iQueryType, NULL, NULL, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
            if (hDevInfo == INVALID_HANDLE_VALUE)
                return -1;

            DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
            for (MemberIndex = 0; ((pMacAddress == NULL) || (iTotal < iSize)); MemberIndex++)
            {
                if (!SetupDiEnumDeviceInterfaces(hDevInfo, NULL, GUID_QUERYSET + iQueryType, MemberIndex, &DeviceInterfaceData))
                    break;

                SetupDiGetDeviceInterfaceDetail(hDevInfo, &DeviceInterfaceData, NULL, 0, &RequiredSize, NULL);

                DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(RequiredSize);
                DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &DeviceInterfaceData, DeviceInterfaceDetailData, RequiredSize, NULL, NULL))
                {
                    if (pMacAddress != NULL)
                    {
                        if (WDK_GetProperty(DeviceInterfaceDetailData->DevicePath, iQueryType, pMacAddress, iTotal))
                            iTotal++;
                    }
                    else
                        iTotal++;
                }
                free(DeviceInterfaceDetailData);
            }
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return iTotal;
        }

    };
#else //Linux
    class CMacAddr
    {
    public:
        CMacAddr() {};
        ~CMacAddr() {};
    public:
        static int GetNetMAC(unsigned char ucbuf[48]) // ret MAC NUMS
        {
            int fd, intrface;
            struct ifreq buf[16];
#ifdef SOLARIS
            struct arpreq arp;
#else
#endif
            struct ifconf ifc;
            if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
                return 0;
            ifc.ifc_len = sizeof buf;
            ifc.ifc_buf = (caddr_t)buf;
            if (ioctl(fd, SIOCGIFCONF, (char *)&ifc))
            {
                close(fd);
                return 0;
            }

            intrface = ifc.ifc_len / sizeof(struct ifreq);
            int i, nmac = 0;

            while (intrface-- > 0)
            {
                if (nmac >= 8)
                    break;
#ifdef SOLARIS

                arp.arp_pa.sa_family = AF_INET;
                arp.arp_ha.sa_family = AF_INET;
                ((struct sockaddr_in*)&arp.arp_pa)->sin_addr.s_addr = ((struct sockaddr_in*)(&buf[intrface].ifr_addr))->sin_addr.s_addr;
                if (!(ioctl(fd, SIOCGARP, (char *)&arp)))
                {
                    if (
                        buf[intrface].arp.arp_ha.sa_data[0] == buf[intrface].arp.arp_ha.sa_data[1] &&
                        buf[intrface].arp.arp_ha.sa_data[1] == buf[intrface].arp.arp_ha.sa_data[2] &&
                        buf[intrface].arp.arp_ha.sa_data[2] == buf[intrface].arp.arp_ha.sa_data[3] &&
                        buf[intrface].arp.arp_ha.sa_data[3] == buf[intrface].arp.arp_ha.sa_data[4] &&
                        buf[intrface].arp.arp_ha.sa_data[4] == buf[intrface].arp.arp_ha.sa_data[5]
                        )
                        continue;
                    for (i = 0; i < 6; i++)
                        ucbuf[nmac * 6 + i] = (unsigned char)arp.arp_ha.sa_data[i];
                    nmac++;
                }
#else
                if (!(ioctl(fd, SIOCGIFHWADDR, (char *)&buf[intrface])))
                {
                    if (
                        buf[intrface].ifr_hwaddr.sa_data[0] == buf[intrface].ifr_hwaddr.sa_data[1] &&
                        buf[intrface].ifr_hwaddr.sa_data[1] == buf[intrface].ifr_hwaddr.sa_data[2] &&
                        buf[intrface].ifr_hwaddr.sa_data[2] == buf[intrface].ifr_hwaddr.sa_data[3] &&
                        buf[intrface].ifr_hwaddr.sa_data[3] == buf[intrface].ifr_hwaddr.sa_data[4] &&
                        buf[intrface].ifr_hwaddr.sa_data[4] == buf[intrface].ifr_hwaddr.sa_data[5]
                        )
                        continue;
                    for (i = 0; i < 6; i++)
                        ucbuf[nmac * 6 + i] = (unsigned char)buf[intrface].ifr_hwaddr.sa_data[i];
                    nmac++;
                }
#endif
            }
            close(fd);
            return nmac;
        }
    };
#endif  // _WIN32

    inline int getnetmac(unsigned char *ucbuf, int nmaxadapters)
    {
        unsigned char tmp[48];
        int n = CMacAddr::GetNetMAC(tmp);
        if (n > nmaxadapters)
            n = nmaxadapters;
        if (n > 0)
            memcpy(ucbuf, tmp, n * 6);
        return n;//Returns the number of network adapters[0,8]
    }
}//namespace ec
