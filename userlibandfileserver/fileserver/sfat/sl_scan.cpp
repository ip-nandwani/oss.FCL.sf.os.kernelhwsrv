// Copyright (c) 1998-2009 Nokia Corporation and/or its subsidiary(-ies).
// All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the License "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
//
// Initial Contributors:
// Nokia Corporation - initial contribution.
//
// Contributors:
//
// Description:
// f32\sfat\sl_scan.cpp
// ScanDrive code, specific for EFAT.FSY
// 
//

/**
 @file
 @internalTechnology
*/


//#define DEBUG_SCANDRIVE

#include "sl_std.h"
#include "sl_scandrv.h"

const TInt KEndOfDirectory			= 0xFFFF;   ///< End of directory marker
const TInt KMaxScanDepth			= 20;       ///< Maximum scan depth of to avoid stack over flow 
const TInt KClusterListGranularity	= 8;        ///< Granularity of cluster list used for storage of clusters when KMaxScanDepth is reached

/**
Creates a CScanDrive

@param aMount The owning mount
*/
CScanDrive* CScanDrive::NewL(CFatMountCB* aMount)
	{
	if(aMount==NULL)
		User::Leave(KErrArgument);
	CScanDrive* self=new (ELeave) CScanDrive();
	CleanupStack::PushL(self);
	self->ConstructL(aMount);
	CleanupStack::Pop();
	return self;
	}


CScanDrive::CScanDrive()
//
// Constructor
//
	{
	}


CScanDrive::~CScanDrive()
//
// Destructor
//
	{
	delete iNewFat;
	for(TInt i=0;i<KMaxArrayDepth && iClusterListArray[i]!=NULL;++i)
		{
		iClusterListArray[i]->Close();
		delete iClusterListArray[i];
		}
	}


void CScanDrive::ConstructL(CFatMountCB* aMount)
//
// Create the new fat and initalise
//
	{
	iMount=aMount;
	iNewFat=CCheckFatTable::NewL(aMount);
	iNewFat->InitializeL();
	}


TBool CScanDrive::AlreadyExistsL(TInt aCluster)const
//
//	Return ETrue if aCluster in the new fat contains a non-zero entry
//
	{
	return(iNewFat->ReadL(aCluster)!=0);
	}


TBool CScanDrive::IsEndOfRootDir(const TEntryPos& aPos)const
//
// Return ETrue if aPos is the last entry in the root directory
//
	{
	return(iMount->IsRootDir(aPos)&&(iMount->StartOfRootDirInBytes()+aPos.iPos==(iMount->RootDirEnd()-KSizeOfFatDirEntry)));
	}

/**
@param aVal Value of the cluster to be tested
@return ETrue if aVal is the end of cluster marker
*/
TBool CScanDrive::IsEofF(TInt aVal)const 
	{
    return iMount->IsEndOfClusterCh(aVal);
	}

/**
@return True if a directory error has been found
*/
TBool CScanDrive::IsDirError()const
	{
	return(iDirError!=0);
	}



/**
    After StartL() and finishing allows us to know if there were any problems at all.
    The client may wish to remount the filesystem if there were errors.

    @return EFalse if there were no problems in FS.
*/
TBool CScanDrive::ProblemsDiscovered() const
{
    return IsDirError() || iFoundProblems;
}

/**
    Sets the flag indicating than there are errors in filesystem structure
    See ProblemsDiscovered()
*/
void CScanDrive::IndicateErrorsFound()
{
    iFoundProblems = ETrue;
}



/**
Start point for scan drive also fixes up errors 

@return The result of the scan
@leave 
*/
TInt CScanDrive::StartL()
	{
	__PRINT(_L("CScanDrive::StartL"));
	// check directory structure
	CheckDirStructureL();
#if defined(DEBUG_SCANDRIVE)
	CompareFatsL();
#endif
	// fix error in directory structure
	if(IsDirError())
		FixupDirErrorL();
	// flush new fat
	WriteNewFatsL();
#if defined(DEBUG_SCANDRIVE)
	PrintErrors();
#endif
	return KErrNone;
	}

/**
Fix errors detected by the drive scan

@leave System wide error code
*/
void CScanDrive::FixupDirErrorL()
	{
	if(!IsDirError())
		return;
	if(iDirError==EScanMatchingEntry)
		{
		FindSameStartClusterL();
		FixMatchingEntryL();
		}
	else
		{
        FixPartEntryL();
        }

    IndicateErrorsFound(); //-- indicate that we have found errors
	}

/**
Find positions of entries with same start cluster for error correction, searches
the whole volume. Starts at the root directory. 

@leave System wide error code
*/
void CScanDrive::FindSameStartClusterL()
	{
	TInt err=FindStartClusterL(0);
	if(err==KErrNone)
		return;
	for(TInt i=0;i<KMaxArrayDepth && iClusterListArray[i]!=NULL;++i)
		{
		RArray<TInt>* clusterList=iClusterListArray[i];
		for(TInt j=0;j<clusterList->Count();++j)
			{
			iRecursiveDepth=0;
			err=FindStartClusterL((*clusterList)[j]);
			if(err==KErrNone)
				return;
			}
		}
	__ASSERT_ALWAYS(err==KErrNone,User::Leave(KErrNotFound));
	}
/**
Scan through directory structure looking for start cluster found in iMatching

@param aDirCluster Start cluster for scan to start
@return System wide error value
@leave 
*/
TInt CScanDrive::FindStartClusterL(TInt aDirCluster)
	{
	__PRINT1(_L("CScanDrive::FindStartCluster dirCluster=%d"),aDirCluster);
	__ASSERT_ALWAYS(aDirCluster>=0,User::Leave(KErrCorrupt));
	if(++iRecursiveDepth==KMaxScanDepth)
		{
		--iRecursiveDepth;
		return(KErrNotFound);
		}
	TEntryPos entryPos(aDirCluster,0);
	TInt dirEntries=0;
	FOREVER
		{
		TFatDirEntry entry;
		iMount->ReadDirEntryL(entryPos,entry);
		if(entry.IsParentDirectory()||entry.IsCurrentDirectory()||entry.IsErased())
			{
			if(IsEndOfRootDir(entryPos))
				break;
			iMount->MoveToNextEntryL(entryPos);
			continue;
			}
		if(entry.IsEndOfDirectory())
			break;
		TBool isComplete;
		TEntryPos vfatPos=entryPos;
		isComplete=MoveToVFatEndL(entryPos,entry,dirEntries);
		__ASSERT_ALWAYS(isComplete,User::Leave(KErrBadName));
		TInt err=CheckEntryClusterL(entry,vfatPos);
		if(err==KErrNone)
			{
			--iRecursiveDepth;
			return(err);
			}
		if(IsEndOfRootDir(entryPos))
			break;
		iMount->MoveToNextEntryL(entryPos);
		}
	--iRecursiveDepth;
	return(KErrNotFound);
	}

/**
Procces aEntry to find matching start cluster

@param aEntry Directory entry to check
@param aEntryPos Position of directory to check
@return System wide error value
@leave 
*/
TInt CScanDrive::CheckEntryClusterL(const TFatDirEntry& aEntry, const TEntryPos& aEntryPos)
	{
	__PRINT(_L("CScanDrive::CheckEntryClusterL"));
	if(iMount->StartCluster(aEntry)==iMatching.iStartCluster)
		{
		TBool complete=AddMatchingEntryL(aEntryPos);
		if(complete)
			return(KErrNone);
		}
	else if(aEntry.Attributes()&KEntryAttDir)
		return(FindStartClusterL(iMount->StartCluster(aEntry)));
	return(KErrNotFound);
	}

/**
Checks directory strucutre for errors, can be considered the start point of the scan.  
Handles recursion depth to avoid stack overflow.

@leave System wide error code
*/
void CScanDrive::CheckDirStructureL()
	{
	CheckDirL(iMount->RootIndicator());
	// Due to recursive nature of CheckDirL when a depth of
	// KMaxScanDepth is reached clusters are stored in a list
	// and passed into CheckDirL afresh
	for(TInt i=0;i<KMaxArrayDepth && iClusterListArray[i]!=NULL;++i)
		{
		RArray<TInt>* clusterList=iClusterListArray[i];
		++iListArrayIndex;
		for(TInt j=0;j<clusterList->Count();++j)
			{
			iRecursiveDepth=0;
			CheckDirL((*clusterList)[j]);
			}
		}
	}
/**
Function is called recursively with Process entry untill the whole volume has been scanned.
Each directory entry is scanned for errors, these are recorded for later fixing. 

@param aCluster Directory cluster to start checking
@leave System wide error codes
*/
void CScanDrive::CheckDirL(TInt aCluster)
	{
	__PRINT1(_L("CScanDrive::CheckDirL aCluster=%d"),aCluster);
	__ASSERT_ALWAYS(aCluster>=0,User::Leave(KErrCorrupt));
	// check depth of recursion
	if(++iRecursiveDepth==KMaxScanDepth)
		{
		AddToClusterListL(aCluster);
		--iRecursiveDepth;
		return;
		}
#if defined(DEBUG_SCANDRIVE)
	++iDirsChecked;
#endif
	TEntryPos entryPos(aCluster,0);
	TInt dirEntries=0;
	FOREVER
		{
		TFatDirEntry entry;
		iMount->ReadDirEntryL(entryPos,entry);
		if(!iMount->IsEndOfClusterCh(entryPos.iCluster))
			++dirEntries;
		if(entry.IsParentDirectory()||entry.IsCurrentDirectory()||entry.IsErased())
			{
			if(IsEndOfRootDir(entryPos))
				break;
			iMount->MoveToNextEntryL(entryPos);
			continue;
			}
		if(entry.IsEndOfDirectory())
			{
			if(aCluster)	
				WriteClusterChainL(aCluster,dirEntries<<KSizeOfFatDirEntryLog2);
			break;
			}
		TEntryPos origPos=entryPos;
		TFatDirEntry origEntry=entry;
		TInt origDirEntries=dirEntries;
		TBool isComplete;
		isComplete=MoveToVFatEndL(entryPos,entry,dirEntries);
		// Only assume that this is a corrupted VFAT entry if the VFAT attributes are set; 
		// assuming a non-VFAT corrupted entry is a VFAT entry is dangerous as we then assume that the 
		// first byte is a count of entries to skip, thus completely invalidating the next <n> directories.
		if (!isComplete && origEntry.IsVFatEntry())
			{
			AddPartialVFatL(origPos,origEntry);
			if(entryPos.iCluster!=KEndOfDirectory)
				{
				TInt toMove=origEntry.NumFollowing()-(dirEntries-origDirEntries);
				if(toMove)
					MovePastEntriesL(entryPos,entry,toMove,dirEntries);
				}
			else
				{
				// we fell off the end of the directory file, so just strip this
				// incomplete long file name entry
				dirEntries = origDirEntries;
				}
			}
		else
			ProcessEntryL(entry);
		if(IsEndOfRootDir(entryPos))
			break;
		iMount->MoveToNextEntryL(entryPos);
		}
	--iRecursiveDepth;
	}

/**
Process non trivial entries, such as files, if they are correct by filling out their 
cluster allocation in the bit packed Fat table. If it comes accross a directory 
CheckDirL will be called.

@param aEntry Directory entry to check
@leave System wide error code
*/
void CScanDrive::ProcessEntryL(const TFatDirEntry& aEntry)
	{
	__PRINT(_L("CScanDrive::ProcessEntryL"));
	TInt entryAtt=aEntry.Attributes();
	__ASSERT_ALWAYS(!(entryAtt&~KEntryAttMaskSupported)&&!aEntry.IsErased(),User::Leave(KErrCorrupt));
	if(!(entryAtt&(KEntryAttDir|KEntryAttVolume)) && iMount->StartCluster(aEntry)>0)
		WriteClusterChainL(iMount->StartCluster(aEntry),aEntry.Size());
	else if(entryAtt&KEntryAttDir)
		CheckDirL(iMount->StartCluster(aEntry));
	}

/**
Writes out the cluster chain for a correct file or directory, checks that the cluster 
has not already been used and that the correct number of clusters are allocated for the 
size of file. Registers cluster as used if correct

@param aCluster Cluster chain start point
@param aSizeInBytes Size of the file or directory in bytes
@leave System wide error values
*/
void CScanDrive::WriteClusterChainL(TInt aCluster,TInt aSizeInBytes)
//
// Mark off in the new fat the clusters used by entry with start cluster of aCluster
//
	{

    IndicateErrorsFound(); //-- indicate that we have found errors

	__PRINT1(_L("CScanDrive::WriteClusterChainL starting at %d"),aCluster);
	__ASSERT_ALWAYS(aCluster>0 && aSizeInBytes>=0,User::Leave(KErrCorrupt));
	TInt clusterCount;
	if(aSizeInBytes==0)
		clusterCount=1;
	else
		clusterCount=(aSizeInBytes+(1<<iMount->ClusterSizeLog2())-1)>>iMount->ClusterSizeLog2();
	TInt startCluster=aCluster;
	while(clusterCount)
		{
		if(AlreadyExistsL(aCluster))
			{
			__ASSERT_ALWAYS(!IsDirError()&&iMatching.iStartCluster==0&&aCluster==startCluster,User::Leave(KErrCorrupt));
			iMatching.iStartCluster=aCluster;
			iDirError=EScanMatchingEntry;
			return;
			}
		if(clusterCount==1)
			{
			iNewFat->WriteFatEntryEofFL(aCluster);
			return;
			}
		else
			{
			TInt clusterVal;
			clusterVal=iMount->FAT().ReadL(aCluster);
			__ASSERT_ALWAYS(!IsEofF(clusterVal) && clusterVal!=0,User::Leave(KErrCorrupt));
			iNewFat->WriteL(aCluster,clusterVal);
			aCluster=clusterVal;
			--clusterCount;
			}
		}
	}

/**
Move to dos entry, checking all vfat entry ID numbers are in sequence.
Assumes aEntry is not erased

@param aPos Position of the entry to move from, returns with new position
@param aEntry The Dos entry after the Vfat entries on return
@param aDirLength Running total of the length of the directory in entries
@leave System wide error codes
@return EFalse if not valid vfat entries or dos entry, else returns ETrue
*/
TBool CScanDrive::MoveToVFatEndL(TEntryPos& aPos,TFatDirEntry& aEntry,TInt& aDirLength)
	{
	__PRINT2(_L("CScanDrive::MoveToVFatEndL cluster=%d,pos=%d"),aPos.iCluster,aPos.iPos);
	if(!aEntry.IsVFatEntry())
		return IsDosEntry(aEntry);
	TInt toFollow=aEntry.NumFollowing();
	__ASSERT_ALWAYS(toFollow>0&&!aEntry.IsErased(),User::Leave(KErrCorrupt));
	FOREVER
		{
		iMount->MoveToNextEntryL(aPos);
		iMount->ReadDirEntryL(aPos,aEntry);
		++aDirLength;
		--toFollow;
		if(!toFollow)
			break;
		if(!IsValidVFatEntry(aEntry,toFollow))
			return(EFalse);
		}
	return(IsDosEntry(aEntry));
	}

/**
Check if an entry is valid VFat

@param aEntry Entry to check
@param aPrevNum Number into VFat entries for a dos entry to ensure in correct position
@return ETrue if aEntry is a valid vfat entry
*/
TBool CScanDrive::IsValidVFatEntry(const TFatDirEntry& aEntry, TInt aPrevNum)const
	{
	if(aEntry.IsErased()||!aEntry.IsVFatEntry())
		return(EFalse);
	return(aEntry.NumFollowing()==aPrevNum);
	}

/**
Check if an entry is a Dos entry

@param aEntry Entry to check
@return ETrue if aEntry is a dos entry
*/
TBool CScanDrive::IsDosEntry(const TFatDirEntry& aEntry)const
	{
	TBool res = !(aEntry.Attributes()&~KEntryAttMaskSupported) && !aEntry.IsErased() && !aEntry.IsVFatEntry() && !aEntry.IsEndOfDirectory();
	return res;
	} 

/**
Add partial entry to iPartEntry under the error condition of not all Vfat entries 
being present

@param aStartPos Position of the Dos entry associated with the VFat entries
@param aEntry Directory Entry of the Dos entry associated with the VFat entries
@leave KErrCorrupt Occurs if the entry is not valid
*/
void CScanDrive::AddPartialVFatL(const TEntryPos& aStartPos, const TFatDirEntry& aEntry)
	{
	__PRINT2(_L("CScanDrive::AddPartialVFatL cluster=%d pos=%d"),aStartPos.iCluster,aStartPos.iPos);
	__ASSERT_ALWAYS(!IsDirError(),User::Leave(KErrCorrupt));
	iPartEntry.iEntryPos=aStartPos;
	iPartEntry.iEntry=aEntry;
	iDirError=EScanPartEntry;
	}

/**
Add entry position to iMatching

@param aEntryPos Position of the entry with the matching entry
@leave KErrCorrupt if the start cluster is 0 or more that two matching entries occurs
@return 
*/
TBool CScanDrive::AddMatchingEntryL(const TEntryPos& aEntryPos)
	{
	__PRINT2(_L("CScanDrive::AddMatchingEntryL cluster=%d pos=%d"),aEntryPos.iCluster,aEntryPos.iPos);
	__ASSERT_ALWAYS(iMatching.iStartCluster>0 && iMatching.iCount<KMaxMatchingEntries,User::Leave(KErrCorrupt));
	iMatching.iEntries[iMatching.iCount++]=aEntryPos;
	return iMatching.iCount==KMaxMatchingEntries;
	}


/**
Scan for differnces in the new and old FAT table writing them to media if discovered

@leave System wide error codes
*/
void CScanDrive::WriteNewFatsL()
//
// Write the new fat table to disk
//
	{
	if(iNewFat->FlushL())
        IndicateErrorsFound(); //-- indicate that we have found errors       
	}

TInt CScanDrive::GetReservedidL(TEntryPos aVFatPos)
//
// Return the id found in reserved2 field of dos entry
//
	{
	__PRINT(_L("CScanDrive::GetReservedidL"));
	TFatDirEntry entry;
	iMount->ReadDirEntryL(aVFatPos,entry);
	if(!IsDosEntry(entry))
		{
		TInt toMove=entry.NumFollowing();
		while(toMove--)
			iMount->MoveToNextEntryL(aVFatPos);
		iMount->ReadDirEntryL(aVFatPos,entry);
		}
	return(entry.RuggedFatEntryId());
	}

/**
Erase part entry found in iPartEntry

@leave System wide error code
*/
void CScanDrive::FixPartEntryL()
	{
	__PRINT2(_L("CScanDrive::FixPartEntryL cluster=%d,pos=%d"),iPartEntry.iEntryPos.iCluster,iPartEntry.iEntryPos.iPos);
	iMount->EraseDirEntryL(iPartEntry.iEntryPos,iPartEntry.iEntry);
    IndicateErrorsFound(); //-- indicate that we have found errors
	}
	
/**
Delete entry with largest value in the reserved2 section(bytes 20 and 21) of dos entry
	
@leave System wide error code
*/
void CScanDrive::FixMatchingEntryL()
	{
	__PRINT1(_L("CScanDrive::FixMatchingEntryL() start cluster=%d"),iMatching.iStartCluster);
	__ASSERT_ALWAYS(iMatching.iCount==KMaxMatchingEntries,User::Leave(KErrCorrupt));
	TInt idOne=GetReservedidL(iMatching.iEntries[0]);
	TInt idTwo=GetReservedidL(iMatching.iEntries[1]);
	TFatDirEntry entry;
	TInt num=idOne>idTwo?0:1;
	iMount->ReadDirEntryL(iMatching.iEntries[num],entry);
	iMount->EraseDirEntryL(iMatching.iEntries[num],entry);
	IndicateErrorsFound(); //-- indicate that we have found errors
    }
/**
Move past specified number of entries

@param aEntryPos Start position to move from, updated as move takes place
@param aEntry Directory entry moved to
@param aToMove Number of entries to move through
@param aDirEntries Number of entries moved, updated as move takes place
@leave System wide error code
*/
void CScanDrive::MovePastEntriesL(TEntryPos& aEntryPos,TFatDirEntry& aEntry,TInt aToMove,TInt& aDirEntries)
	{
	while(aToMove-- && aEntryPos.iCluster!=KEndOfDirectory)
		{
		iMount->MoveToNextEntryL(aEntryPos);
		++aDirEntries;
		}
	iMount->ReadDirEntryL(aEntryPos,aEntry);
	}

/**
Adds aCluster to cluster list array so that it may be revisited later, avoids stack 
over flow

@param aCluster Directory cluster number to add to the list
@leave KErrNoMemory If allocation fails
*/
void CScanDrive::AddToClusterListL(TInt aCluster)
	{
	if(iListArrayIndex>=KMaxArrayDepth)
		return;
	if(iClusterListArray[iListArrayIndex]==NULL)
		iClusterListArray[iListArrayIndex]=new(ELeave) RArray<TInt>(KClusterListGranularity);
	iClusterListArray[iListArrayIndex]->Append(aCluster);
	}


#if defined(DEBUG_SCANDRIVE)
void CScanDrive::CompareFatsL()
//
// Compare new fat and first fat table
//	
	{
	__PRINT(_L("CScanDrive::CompareFatsL()"));
	TInt maxClusters;
	maxClusters=iMount->UsableClusters();
	for(TInt i=KFatFirstSearchCluster; i<maxClusters; ++i)
		{
		TInt realFat=iMount->FAT().ReadL(i);
		TInt newFat=iNewFat->ReadL(i);
		if(realFat!=newFat)
			{
			if(realFat!=0 && newFat==0)
				__PRINT1(_L("Lost cluster=%d\n"),i)
			else if((realFat>0 && !IsEofF(realFat)) && IsEofF(newFat))
				__PRINT1(_L("Hanging cluster = %d\n"),i)
			else if(realFat==0 && newFat>0)
				__PRINT1(_L("Unflushed cluster = %d\n"),i)
			else
				User::Leave(KErrCorrupt);
			}
		}
	}	


/** 
For debug purposes, print errors found as debug output 
 
*/ 
void CScanDrive::PrintErrors()
	{
	__PRINT1(_L("Directories visisted = %d\n"),iDirsChecked);
	if(iDirError==EScanPartEntry)
		__PRINT2(_L("Part entry-dir cluster=%d,dir pos=%d,\n"),iPartEntry.iEntryPos.iCluster,iPartEntry.iEntryPos.iPos)
	else if(iDirError==EScanMatchingEntry)
		{
		__PRINT1(_L("Matching cluster - cluster no=%d\n"),iMatching.iStartCluster);
		__PRINT2(_L("\tcluster 1 - dir cluster=%d,dir pos=%d\n"),iMatching.iEntries[0].iCluster,iMatching.iEntries[0].iPos);
		__PRINT2(_L("\tcluster 2 - dir cluster=%d,dir pos=%d\n"),iMatching.iEntries[1].iCluster,iMatching.iEntries[1].iPos);
		}
	}
	
#endif



