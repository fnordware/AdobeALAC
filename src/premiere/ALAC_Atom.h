///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2014, Brendan Bolles
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *	   Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *	   Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------
//
// ALAC (Apple Lossless) plug-in for Premiere
//
// by Brendan Bolles <brendan@fnordware.com>
//
// ------------------------------------------------------------------------


#ifndef ALAC_ATOM_H
#define ALAC_ATOM_H


#include "Ap4.h"


class ALAC_Atom : public AP4_Atom
{
  public:
	ALAC_Atom(AP4_Size size, AP4_ByteStream &stream);
	ALAC_Atom(void *magic_cookie, size_t magic_cookie_size);
	virtual ~ALAC_Atom();
	
	void *GetMagicCookie(size_t &size);
	
	virtual AP4_Result WriteFields(AP4_ByteStream& stream);
	
  private:
	void *_magic_cookie;
	size_t _magic_cookie_size;
};



class ALAC_TypeHandler : public AP4_AtomFactory::TypeHandler
{
  public:
	virtual ~ALAC_TypeHandler() {};
	virtual AP4_Result CreateAtom(AP4_Atom::Type  type,
									AP4_UI32        size,
									AP4_ByteStream& stream,
									AP4_Atom::Type  context,
									AP4_Atom*&      atom);
};


#endif