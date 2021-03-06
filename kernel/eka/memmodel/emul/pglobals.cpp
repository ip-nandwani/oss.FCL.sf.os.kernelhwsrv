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
// e32\memmodel\emul\pglobals.cpp
// 
//

#include "plat_priv.h"

TInt PP::RamDriveMaxSize;
TLinAddr PP::RamDriveStartAddress;
HANDLE PP::RamDriveFile;
HANDLE PP::RamDriveFileMapping;
TRamDriveInfo* PP::RamDriveInfo;
