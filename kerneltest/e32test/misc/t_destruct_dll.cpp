// Copyright (c) 2007-2009 Nokia Corporation and/or its subsidiary(-ies).
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
// e32test\misc\t_destruct.cpp
// 
//

#include <e32debug.h>
#include <e32msgqueue.h>
#include "t_destruct.h"

class TTestObjectStatic
	{
public:
	TTestObjectStatic();
	~TTestObjectStatic();
	};

TTestObjectStatic GlobalObjectWithDestructor;

void Panic(TInt aReason)
	{
	User::Panic(_L("t_destruct_dll"), aReason);
	}

TTestObjectStatic::TTestObjectStatic()
	{
	RDebug::Printf("t_destruct_dll constructor called\n");
	TInt r;
	RMsgQueue<TMessage> messageQueue;
	r = messageQueue.OpenGlobal(KMessageQueueName);
	if (r != KErrNone)
		Panic(r);
	r = messageQueue.Send(EMessageConstructStatic);
	if (r != KErrNone)
		Panic(r);
	messageQueue.Close();
	}

TTestObjectStatic::~TTestObjectStatic()
	{
	RDebug::Printf("t_destruct_dll destructor called\n");
	TInt r;
	RMsgQueue<TMessage> messageQueue;
	r = messageQueue.OpenGlobal(KMessageQueueName);
	if (r != KErrNone)
		Panic(r);
	r = messageQueue.Send(EMessageDestructStatic);
	if (r != KErrNone)
		Panic(r);
	messageQueue.Close();
	}

EXPORT_C void StaticMain()
	{
	StaticMain3();
	}
