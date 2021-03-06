#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define INCL_DOS
#define INCL_DOSDEVIOCTL
#define INCL_DOSDEVICES
#define INCL_DOSERRORS

#include "os2.h"
#include "portable.h"
#include "fat32ifs.h"


static BOOL RemoveVolume(PVOLINFO pVolInfo);
static USHORT CheckWriteProtect(PVOLINFO);


int far pascal FS_MOUNT(unsigned short usFlag,		/* flag		*/
                        struct vpfsi far * pvpfsi,		/* pvpfsi	*/
                        struct vpfsd far * pvpfsd,		/* pvpfsd	*/
                        unsigned short hVBP,		/* hVPB		*/
                        char far *	pBoot		/* pBoot	*/)
{
PBOOTSECT pSect;
PVOLINFO  pVolInfo;
PVOLINFO  pNext;
USHORT usVolCount;
USHORT hDupVBP;
USHORT rc;
P_DriverCaps pDevCaps;
P_VolChars   pVolChars;

   if (f32Parms.fMessageActive & LOG_FS)
      Message("FS_MOUNT for %c (%d):, flag = %d",
         pvpfsi->vpi_drive + 'A',
         pvpfsi->vpi_unit,
         usFlag);

   switch (usFlag)
      {
      case MOUNT_MOUNT  :

         if (FSH_FINDDUPHVPB(hVBP, &hDupVBP))
            hDupVBP = 0;

         pSect = (PBOOTSECT)pBoot;
         if (memicmp(pSect->FileSystem, "FAT32", 5))
            {
            rc = ERROR_VOLUME_NOT_MOUNTED;
            goto FS_MOUNT_EXIT;
            }

         if (pSect->bpb.BytesPerSector != SECTOR_SIZE)
            {
            rc = ERROR_VOLUME_NOT_MOUNTED;
            goto FS_MOUNT_EXIT;
            }

         pvpfsi->vpi_vid    = pSect->ulVolSerial;
         pvpfsi->vpi_bsize  = pSect->bpb.BytesPerSector;
         pvpfsi->vpi_totsec = pSect->bpb.BigTotalSectors;
         pvpfsi->vpi_trksec = pSect->bpb.SectorsPerTrack;
         pvpfsi->vpi_nhead  = pSect->bpb.Heads;
         memset(pvpfsi->vpi_text, 0, sizeof pvpfsi->vpi_text);
         memcpy(pvpfsi->vpi_text, pSect->VolumeLabel, sizeof pSect->VolumeLabel);

         pVolInfo = gdtAlloc(STORAGE_NEEDED, FALSE);
         if (!pVolInfo)
            {
            rc = ERROR_NOT_ENOUGH_MEMORY;
            goto FS_MOUNT_EXIT;
            }
         rc = FSH_FORCENOSWAP(SELECTOROF(pVolInfo));
         if (rc)
            FatalMessage("FSH_FORCENOSWAP on VOLINFO Segment failed, rc=%u", rc);

         memset(pVolInfo, 0, (size_t)STORAGE_NEEDED);

         InitCache(ulCacheSectors);

         memcpy(&pVolInfo->BootSect, pSect, sizeof (BOOTSECT));
         pVolInfo->ulActiveFatStart = pSect->bpb.ReservedSectors;
         if (pSect->bpb.ExtFlags & 0x0080)
            pVolInfo->ulActiveFatStart +=
               pSect->bpb.BigSectorsPerFat * (pSect->bpb.ExtFlags & 0x000F);
         pVolInfo->ulStartOfData    = pSect->bpb.ReservedSectors +
               pSect->bpb.BigSectorsPerFat * pSect->bpb.NumberOfFATs;

         pVolInfo->pBootFSInfo = (PBOOTFSINFO)(pVolInfo + 1);
         pVolInfo->pbFatSector = (PBYTE)(pVolInfo->pBootFSInfo + 1);
         pVolInfo->ulCurFatSector = -1L;
         pVolInfo->usClusterSize = pSect->bpb.BytesPerSector * pSect->bpb.SectorsPerCluster;
         pVolInfo->ulTotalClusters = (pSect->bpb.BigTotalSectors - pVolInfo->ulStartOfData) / pSect->bpb.SectorsPerCluster;
         pVolInfo->hVBP = hVBP;
         pVolInfo->hDupVBP = hDupVBP;
         pVolInfo->bDrive = pvpfsi->vpi_drive;
         pVolInfo->bUnit  = pvpfsi->vpi_unit;
         pVolInfo->pNextVolInfo = NULL;

         if (usDefaultRASectors == 0xFFFF)
            pVolInfo->usRASectors = (pVolInfo->usClusterSize * 2) / SECTOR_SIZE;
         else
            pVolInfo->usRASectors = usDefaultRASectors;

         if (pVolInfo->usRASectors > (pVolInfo->usClusterSize * 4) / SECTOR_SIZE)
            pVolInfo->usRASectors = (pVolInfo->usClusterSize * 4) / SECTOR_SIZE;

         if (pSect->bpb.FSinfoSec != 0xFFFF)
            {
            ReadSector(pVolInfo, pSect->bpb.FSinfoSec, 1, pVolInfo->pbFatSector, DVIO_OPNCACHE);
            memcpy(pVolInfo->pBootFSInfo, pVolInfo->pbFatSector + FSINFO_OFFSET, sizeof (BOOTFSINFO));
            }
         else
            memset(pVolInfo->pBootFSInfo, 0, sizeof (BOOTFSINFO));

         *((PVOLINFO *)(pvpfsd->vpd_work)) = pVolInfo;

         if (!pGlobVolInfo)
            {
            pGlobVolInfo = pVolInfo;
            usVolCount = 1;
            }
         else
            {
            pNext = pGlobVolInfo;
            usVolCount = 1;
            if (pNext->bDrive == pvpfsi->vpi_drive && !pVolInfo->hDupVBP)
               pVolInfo->hDupVBP = pNext->hVBP;
            while (pNext->pNextVolInfo)
               {
               pNext = (PVOLINFO)pNext->pNextVolInfo;
               if (pNext->bDrive == pvpfsi->vpi_drive && !pVolInfo->hDupVBP)
                  pVolInfo->hDupVBP = pNext->hVBP;
               usVolCount++;
               }
            pNext->pNextVolInfo = pVolInfo;
            usVolCount++;
            }
         if (f32Parms.fMessageActive & LOG_FS)
            Message("%u Volumes mounted!", usVolCount);
         rc = CheckWriteProtect(pVolInfo);
         if (rc && rc != ERROR_WRITE_PROTECT)
            {
            Message("Cannot access drive, rc = %u", rc);
            goto FS_MOUNT_EXIT;
            }
         if (rc == ERROR_WRITE_PROTECT)
            pVolInfo->fWriteProtected = TRUE;

         pVolInfo->fDiskCleanOnMount = pVolInfo->fDiskClean = GetDiskStatus(pVolInfo);
         if (!pVolInfo->fDiskCleanOnMount)
            Message("DISK IS DIRTY!");
         if (pVolInfo->fWriteProtected)
            pVolInfo->fDiskCleanOnMount = TRUE;

         if (!pVolInfo->hDupVBP &&
            (pVolInfo->pBootFSInfo->ulFreeClusters == 0xFFFFFFFF ||
             !pVolInfo->fDiskClean ||
             pVolInfo->BootSect.bpb.FSinfoSec == 0xFFFF))
            GetFreeSpace(pVolInfo);

         pDevCaps  = pvpfsi->vpi_pDCS;
         pVolChars = pvpfsi->vpi_pVCS;

         if (pDevCaps->Capabilities & GDC_DD_Read2)
            Message("Read2 supported");
         if (pDevCaps->Capabilities & GDC_DD_DMA_Word)
            Message("DMA on word alligned buffers supported");
         if (pDevCaps->Capabilities & GDC_DD_DMA_Byte)
            Message("DMA on byte alligned buffers supported");
         if (pDevCaps->Capabilities & GDC_DD_Mirror)
            Message("Disk Mirroring supported");
         if (pDevCaps->Capabilities & GDC_DD_Duplex)
            Message("Disk Duplexing supported");
         if (pDevCaps->Capabilities & GDC_DD_No_Block)
            Message("Strategy2 does not block");
         if (pDevCaps->Capabilities & GDC_DD_16M)
            Message(">16M supported");
         if (pDevCaps->Strategy2)
            {
            Message("Strategy2   address at %lX", pDevCaps->Strategy2);
            Message("ChgPriority address at %lX", pDevCaps->ChgPriority);

            pVolInfo->pfnStrategy = (STRATFUNC)pDevCaps->Strategy2;
            pVolInfo->pfnPriority = (STRATFUNC)pDevCaps->ChgPriority;
            }

         rc = 0;
         break;

     case MOUNT_VOL_REMOVED: 
     case MOUNT_RELEASE:
         pVolInfo = GetVolInfo(hVBP);
         if (!pVolInfo->hDupVBP)
            UpdateFSInfo(pVolInfo);
         RemoveVolume(pVolInfo);
         freeseg(pVolInfo);
         rc = 0;
         break;

      default :
         rc = ERROR_NOT_SUPPORTED;
         break;
      }

FS_MOUNT_EXIT:
   if (f32Parms.fMessageActive & LOG_FS)
      Message("FS_MOUNT returned %u\n", rc);
   return rc;
}

USHORT CheckWriteProtect(PVOLINFO pVolInfo)
{
USHORT rc;
USHORT usSectors = 1;

   if (f32Parms.fMessageActive & LOG_FUNCS)
      Message("CheckWriteProtect");

   rc = FSH_DOVOLIO(DVIO_OPREAD, DVIO_ALLACK, pVolInfo->hVBP, pVolInfo->pbFatSector, &usSectors, 1L);
   if (!rc)
      {
      usSectors = 1;
      rc = FSH_DOVOLIO(DVIO_OPWRITE, DVIO_ALLACK, pVolInfo->hVBP, pVolInfo->pbFatSector, &usSectors, 1L);
      }

   return rc;
}

BOOL RemoveVolume(PVOLINFO pVolInfo)
{
PVOLINFO pNext;
USHORT rc;

   if (pGlobVolInfo == pVolInfo)
      {
      pGlobVolInfo = pVolInfo->pNextVolInfo;
      return TRUE;
      }

   pNext = (PVOLINFO)pGlobVolInfo;
   while (pNext)
      {
      rc = MY_PROBEBUF(PB_OPWRITE, (PBYTE)pNext, sizeof (VOLINFO));
      if (rc)
         {
         FatalMessage("FAT32: Protection VIOLATION in RemoveVolume!\n");
         return FALSE;
         }

      if (pNext->pNextVolInfo == pVolInfo)
         {
         pNext->pNextVolInfo = pVolInfo->pNextVolInfo;
         return TRUE;
         }
      pNext = (PVOLINFO)pNext->pNextVolInfo;
      }
   return FALSE;
}
