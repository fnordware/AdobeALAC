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


#include "ALAC_Atom.h"


ALAC_Atom::ALAC_Atom(AP4_Size size, AP4_ByteStream &stream) :
	AP4_Atom(AP4_ATOM_TYPE_ALAC, size, 0, 0),
	_magic_cookie(NULL),
	_magic_cookie_size(size - GetHeaderSize())
{
	if(_magic_cookie_size > 0)
	{
		_magic_cookie = malloc(_magic_cookie_size);
		
		// Here I'm moving the file pointer forward by a uint_32 (4 bytes).
		// Not totally sure why I have to do this.  I think it might be because
		// this Atom "IsFull", which seems to mean that it has a 32-bit version/flags
		// entry after the usual size and 'alac' entries, 4 bytes each.  But for some reason
		// the file marker isn't taking into account the version/flags bytes, which appear
		// to be 0 anyway.  So here I'm moving it over to where the ALAC "magic cookie" really starts.
		AP4_Position pos = 0;
		stream.Tell(pos);
		stream.Seek(pos + 4);
		
		if(_magic_cookie)
		{
			stream.Read(_magic_cookie, _magic_cookie_size);
		}
	}
}


ALAC_Atom::ALAC_Atom(void *magic_cookie, size_t magic_cookie_size) :
	AP4_Atom(AP4_ATOM_TYPE_ALAC, (AP4_UI32)magic_cookie_size + 12, 0, 0),
	_magic_cookie(NULL),
	_magic_cookie_size(magic_cookie_size)
{
	if(magic_cookie != NULL && magic_cookie_size != 0)
	{
		_magic_cookie = malloc(_magic_cookie_size);
		
		memcpy(_magic_cookie, magic_cookie, magic_cookie_size);
	}
}


ALAC_Atom::~ALAC_Atom()
{
	if(_magic_cookie)
	{
		free(_magic_cookie);
		
		_magic_cookie = NULL;
		_magic_cookie_size = 0;
	}
}


void *
ALAC_Atom::GetMagicCookie(size_t &size)
{
	size = _magic_cookie_size;
	
	return _magic_cookie;
}


AP4_Result
ALAC_Atom::WriteFields(AP4_ByteStream& stream)
{
	return stream.Write(_magic_cookie, _magic_cookie_size);
}



AP4_Result
ALAC_TypeHandler::CreateAtom(AP4_Atom::Type  type,
								AP4_UI32        size,
								AP4_ByteStream& stream,
								AP4_Atom::Type  context,
								AP4_Atom*&      atom)
{
	if(type == AP4_ATOM_TYPE_ALAC)
	{
		atom = new ALAC_Atom(size, stream);
	
		return AP4_SUCCESS;
	}
	
	return AP4_FAILURE;
}
