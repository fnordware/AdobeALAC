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


#include "ALAC_Premiere_Import.h"


#include "Ap4.h"

#include "ALACBitUtilities.h"
#include "ALACDecoder.h"

#include "ALAC_Atom.h"


#include <assert.h>
#include <math.h>

#include <sstream>


class My_ByteStream : public AP4_ByteStream
{
  public:
	My_ByteStream(imFileRef fp);
	virtual ~My_ByteStream() {}
	
	virtual AP4_Result ReadPartial(void *buffer, AP4_Size bytes_to_read, AP4_Size &bytes_read);
    virtual AP4_Result WritePartial(const void *buffer, AP4_Size bytes_to_write, AP4_Size &bytes_written);
	virtual AP4_Result Seek(AP4_Position position);
	virtual AP4_Result Tell(AP4_Position &position);
	virtual AP4_Result GetSize(AP4_LargeSize &size);
	
    virtual void AddReference();
    virtual void Release();

  private:
	const imFileRef _fp;
	AP4_Cardinal    _refCount;
};


My_ByteStream::My_ByteStream(imFileRef fp) :
	_fp(fp),
	_refCount(1)
{
	Seek(0);
}


AP4_Result
My_ByteStream::ReadPartial(void *buffer, AP4_Size bytes_to_read, AP4_Size &bytes_read)
{
#ifdef PRWIN_ENV	
	DWORD count = bytes_to_read, out;
	
	BOOL result = ReadFile(_fp, buffer, count, &out, NULL);
	
	bytes_read = out;

	return (result ? AP4_SUCCESS : AP4_FAILURE);
#else
	ByteCount count = bytes_to_read, out = 0;
	
	OSErr result = FSReadFork(CAST_REFNUM(_fp), fsAtMark, 0, count, buffer, &out);
	
	bytes_read = out;

	return (result == noErr ? AP4_SUCCESS : AP4_FAILURE);
#endif
}


AP4_Result
My_ByteStream::WritePartial(const void *buffer, AP4_Size bytes_to_write, AP4_Size &bytes_written)
{
	return AP4_ERROR_NOT_SUPPORTED;
}


AP4_Result
My_ByteStream::Seek(AP4_Position position)
{
#ifdef PRWIN_ENV
	LARGE_INTEGER lpos;

	lpos.QuadPart = position;

#if _MSC_VER < 1300
	DWORD pos = SetFilePointer(_fp, lpos.u.LowPart, &lpos.u.HighPart, FILE_BEGIN);

	BOOL result = (pos != 0xFFFFFFFF || NO_ERROR == GetLastError());
#else
	BOOL result = SetFilePointerEx(_fp, lpos, NULL, FILE_BEGIN);
#endif

	return (result ? AP4_SUCCESS : AP4_FAILURE);
#else
	OSErr result = FSSetForkPosition(CAST_REFNUM(_fp), fsFromStart, position);

	return (result == noErr ? AP4_SUCCESS : AP4_FAILURE);
#endif
}


AP4_Result
My_ByteStream::Tell(AP4_Position &position)
{
#ifdef PRWIN_ENV
	LARGE_INTEGER lpos, zero;

	zero.QuadPart = 0;

	BOOL result = SetFilePointerEx(_fp, zero, &lpos, FILE_CURRENT);

	position = lpos.QuadPart;
	
	return (result ? AP4_SUCCESS : AP4_FAILURE);
#else
	long pos;
	SInt64 lpos;

	OSErr result = FSGetForkPosition(CAST_REFNUM(_fp), &lpos);
	
	position = lpos;
	
	return (result == noErr ? AP4_SUCCESS : AP4_FAILURE);
#endif
}


AP4_Result
My_ByteStream::GetSize(AP4_LargeSize &size)
{
#ifdef PRWIN_ENV
	size = GetFileSize(_fp, NULL);
	
	return AP4_SUCCESS;
#else
	SInt64 fork_size = 0;
	
	OSErr result = FSGetForkSize(CAST_REFNUM(_fp), &fork_size);
	
	size = fork_size;
		
	return (result == noErr ? AP4_SUCCESS : AP4_FAILURE);
#endif
}


void
My_ByteStream::AddReference()
{
	_refCount++;
}


void
My_ByteStream::Release()
{
	_refCount--;
}


#pragma mark-


#if IMPORTMOD_VERSION <= IMPORTMOD_VERSION_9
typedef PrSDKPPixCacheSuite2 PrCacheSuite;
#define PrCacheVersion	kPrSDKPPixCacheSuiteVersion2
#else
typedef PrSDKPPixCacheSuite PrCacheSuite;
#define PrCacheVersion	kPrSDKPPixCacheSuiteVersion
#endif



typedef struct
{	
	csSDK_int32				importerID;
	csSDK_int32				fileType;
	int						numChannels;
	int						audioSampleRate;
	int						bitDepth;
	PrAudioSample			duration;
	
	My_ByteStream			*reader;
	AP4_File				*file;
	AP4_Track				*audio_track;
	ALACDecoder				*alac;
	
} ImporterLocalRec8, *ImporterLocalRec8Ptr, **ImporterLocalRec8H;


static const csSDK_int32 ALAC_filetype = 'ALAC';


static prMALError 
SDKInit(
	imStdParms		*stdParms, 
	imImportInfoRec	*importInfo)
{
	importInfo->canSave				= kPrFalse;		// Can 'save as' files to disk, real file only.
	importInfo->canDelete			= kPrFalse;		// File importers only, use if you only if you have child files
	importInfo->canCalcSizes		= kPrFalse;		// These are for importers that look at a whole tree of files so
													// Premiere doesn't know about all of them.
	importInfo->canTrim				= kPrFalse;
	
	importInfo->hasSetup			= kPrFalse;		// Set to kPrTrue if you have a setup dialog
	importInfo->setupOnDblClk		= kPrFalse;		// If user dbl-clicks file you imported, pop your setup dialog
	
	importInfo->dontCache			= kPrFalse;		// Don't let Premiere cache these files
	importInfo->keepLoaded			= kPrFalse;		// If you MUST stay loaded use, otherwise don't: play nice

#ifdef NDEBUG
	// QuickTime can handle these files, so we'll let it.  Premiere can fall back to us if it needs to.
	importInfo->priority			= 0;
#else
	// When we're developing, however, we don't mind getting a little pushy.
	importInfo->priority			= 100;
#endif
	
	importInfo->avoidAudioConform	= kPrTrue;		// If I let Premiere conform the audio, I get silence when
													// I try to play it in the program.  Seems like a bug to me.

	
	
	AP4_DefaultAtomFactory::Instance.AddTypeHandler(new ALAC_TypeHandler);
	

	return malNoError;
}


static prMALError 
SDKGetIndFormat(
	imStdParms		*stdParms, 
	csSDK_size_t	index, 
	imIndFormatRec	*SDKIndFormatRec)
{
	prMALError	result		= malNoError;
	
	switch(index)
	{
		case 0:	
			do{	
				char formatname[255]	= "ALAC";
				char shortname[32]		= "ALAC";
				char platformXten[256]	= "m4a\0\0";

				SDKIndFormatRec->filetype			= ALAC_filetype;

				SDKIndFormatRec->canWriteTimecode	= kPrFalse;
				SDKIndFormatRec->canWriteMetaData	= kPrFalse;

				SDKIndFormatRec->flags = xfCanImport | xfIsSound;

				#ifdef PRWIN_ENV
				strcpy_s(SDKIndFormatRec->FormatName, sizeof (SDKIndFormatRec->FormatName), formatname);				// The long name of the importer
				strcpy_s(SDKIndFormatRec->FormatShortName, sizeof (SDKIndFormatRec->FormatShortName), shortname);		// The short (menu name) of the importer
				strcpy_s(SDKIndFormatRec->PlatformExtension, sizeof (SDKIndFormatRec->PlatformExtension), platformXten);	// The 3 letter extension
				#else
				strcpy(SDKIndFormatRec->FormatName, formatname);			// The Long name of the importer
				strcpy(SDKIndFormatRec->FormatShortName, shortname);		// The short (menu name) of the importer
				strcpy(SDKIndFormatRec->PlatformExtension, platformXten);	// The 3 letter extension
				#endif
			}while(0);
			break;
		default:
			result = imBadFormatIndex;
	}

	return result;
}


prMALError 
SDKOpenFile8(
	imStdParms		*stdParms, 
	imFileRef		*SDKfileRef, 
	imFileOpenRec8	*SDKfileOpenRec8)
{
	prMALError			result = malNoError;

	ImporterLocalRec8H	localRecH = NULL;
	ImporterLocalRec8Ptr localRecP = NULL;

	if(SDKfileOpenRec8->privatedata)
	{
		localRecH = (ImporterLocalRec8H)SDKfileOpenRec8->privatedata;

		stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(localRecH));

		localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *localRecH );
	}
	else
	{
		localRecH = (ImporterLocalRec8H)stdParms->piSuites->memFuncs->newHandle(sizeof(ImporterLocalRec8));
		SDKfileOpenRec8->privatedata = (PrivateDataPtr)localRecH;

		stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(localRecH));

		localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *localRecH );
		
		localRecP->reader = NULL;
		localRecP->file = NULL;
		localRecP->audio_track = NULL;
		localRecP->alac = NULL;
		
		localRecP->importerID = SDKfileOpenRec8->inImporterID;
		localRecP->fileType = SDKfileOpenRec8->fileinfo.filetype;
	}


	SDKfileOpenRec8->fileinfo.fileref = *SDKfileRef = reinterpret_cast<imFileRef>(imInvalidHandleValue);


	if(localRecP)
	{
		const prUTF16Char *path = SDKfileOpenRec8->fileinfo.filepath;
	
	#ifdef PRWIN_ENV
		HANDLE fileH = CreateFileW(path,
									GENERIC_READ,
									FILE_SHARE_READ,
									NULL,
									OPEN_EXISTING,
									FILE_ATTRIBUTE_NORMAL,
									NULL);
									
		if(fileH != imInvalidHandleValue)
		{
			SDKfileOpenRec8->fileinfo.fileref = *SDKfileRef = fileH;
		}
		else
			result = imFileOpenFailed;
	#else
		FSIORefNum refNum = CAST_REFNUM(imInvalidHandleValue);
				
		CFStringRef filePathCFSR = CFStringCreateWithCharacters(NULL, path, prUTF16CharLength(path));
													
		CFURLRef filePathURL = CFURLCreateWithFileSystemPath(NULL, filePathCFSR, kCFURLPOSIXPathStyle, false);
		
		if(filePathURL != NULL)
		{
			FSRef fileRef;
			Boolean success = CFURLGetFSRef(filePathURL, &fileRef);
			
			if(success)
			{
				HFSUniStr255 dataForkName;
				FSGetDataForkName(&dataForkName);
			
				OSErr err = FSOpenFork(	&fileRef,
										dataForkName.length,
										dataForkName.unicode,
										fsRdPerm,
										&refNum);
			}
										
			CFRelease(filePathURL);
		}
									
		CFRelease(filePathCFSR);

		if(CAST_FILEREF(refNum) != imInvalidHandleValue)
		{
			SDKfileOpenRec8->fileinfo.fileref = *SDKfileRef = CAST_FILEREF(refNum);
		}
		else
			result = imFileOpenFailed;
	#endif

	}

	if(result == malNoError)
	{
		localRecP->fileType = SDKfileOpenRec8->fileinfo.filetype;
		
		try
		{
			localRecP->reader = new My_ByteStream(*SDKfileRef);
			
			localRecP->file = new AP4_File(*localRecP->reader);
			
			
			AP4_Track *audio_track = localRecP->file->GetMovie()->GetTrack(AP4_Track::TYPE_AUDIO);
			
			if(audio_track != NULL)
			{
				assert(audio_track->GetSampleDescriptionCount() == 1);
				
				AP4_SampleDescription *desc = audio_track->GetSampleDescription(0);
				
				AP4_AudioSampleDescription *audio_desc = AP4_DYNAMIC_CAST(AP4_AudioSampleDescription, desc);
				
				if(desc != NULL && desc->GetFormat() == AP4_SAMPLE_FORMAT_ALAC)
				{
					localRecP->audio_track = audio_track;
					
					ALAC_Atom *alac_atom = AP4_DYNAMIC_CAST(ALAC_Atom, desc->GetDetails().GetChild(AP4_SAMPLE_FORMAT_ALAC));
					
					if(alac_atom != NULL)
					{
						size_t magic_cookie_size = 0;
						
						void *magic_cookie = alac_atom->GetMagicCookie(magic_cookie_size);
						
						if(magic_cookie != NULL && magic_cookie_size > 0)
						{
							localRecP->alac = new ALACDecoder();
							
							int32_t alac_result = localRecP->alac->Init(magic_cookie, magic_cookie_size);
							
							if(alac_result != 0)
							{
								result = imBadHeader;
							}
						}
						else
							result = imBadHeader;
					}
					else
						result = imBadHeader;
				}
				else
					result = imUnsupportedCompression;
			}
			else
				result = imFileHasNoImportableStreams;
		}
		catch(...)
		{
			result = imBadFile;
		}
	}
	
	// close file and delete private data if we got a bad file
	if(result != malNoError)
	{
		if(SDKfileOpenRec8->privatedata)
		{
			stdParms->piSuites->memFuncs->disposeHandle(reinterpret_cast<PrMemoryHandle>(SDKfileOpenRec8->privatedata));
			SDKfileOpenRec8->privatedata = NULL;
		}
	}
	else
	{
		stdParms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(SDKfileOpenRec8->privatedata));
	}

	return result;
}



static prMALError 
SDKQuietFile(
	imStdParms			*stdParms, 
	imFileRef			*SDKfileRef, 
	void				*privateData)
{
	// "Quiet File" really means close the file handle, but we're still
	// using it and might open it again, so hold on to any stored data
	// structures you don't want to re-create.

	// If file has not yet been closed
	if(SDKfileRef && *SDKfileRef != imInvalidHandleValue)
	{
		ImporterLocalRec8H ldataH	= reinterpret_cast<ImporterLocalRec8H>(privateData);

		stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));

		ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );


		if(localRecP->file)
		{
			delete localRecP->file;
			
			localRecP->file = NULL;
		}

		if(localRecP->reader)
		{
			delete localRecP->reader;
			
			localRecP->reader = NULL;
		}
	
		localRecP->audio_track = NULL;
		
		if(localRecP->alac)
		{
			delete localRecP->alac;
			
			localRecP->alac = NULL;
		}
		

		stdParms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(ldataH));

	#ifdef PRWIN_ENV
		CloseHandle(*SDKfileRef);
	#else
		FSCloseFork( CAST_REFNUM(*SDKfileRef) );
	#endif
	
		*SDKfileRef = imInvalidHandleValue;
	}

	return malNoError; 
}


static prMALError 
SDKCloseFile(
	imStdParms			*stdParms, 
	imFileRef			*SDKfileRef,
	void				*privateData) 
{
	ImporterLocalRec8H ldataH	= reinterpret_cast<ImporterLocalRec8H>(privateData);
	
	// If file has not yet been closed
	if(SDKfileRef && *SDKfileRef != imInvalidHandleValue)
	{
		SDKQuietFile(stdParms, SDKfileRef, privateData);
	}

	// Remove the privateData handle.
	// CLEANUP - Destroy the handle we created to avoid memory leaks
	if(ldataH && *ldataH)
	{
		stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));

		ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );;

		stdParms->piSuites->memFuncs->disposeHandle(reinterpret_cast<PrMemoryHandle>(ldataH));
	}

	return malNoError;
}



static prMALError 
SDKAnalysis(
	imStdParms		*stdParms,
	imFileRef		SDKfileRef,
	imAnalysisRec	*SDKAnalysisRec)
{
	// Is this all I'm supposed to do here?
	// The string shows up in the properties dialog.
	ImporterLocalRec8H ldataH = reinterpret_cast<ImporterLocalRec8H>(SDKAnalysisRec->privatedata);
	ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );

	std::stringstream ss;
	
	// actually, this is already reported, what do I have to add?
	ss << localRecP->numChannels << " channels, " <<
		localRecP->audioSampleRate << " Hz, " <<
		localRecP->bitDepth << "-bit";
	
	if(SDKAnalysisRec->buffersize > ss.str().size())
		strcpy(SDKAnalysisRec->buffer, ss.str().c_str());

	return malNoError;
}




prMALError 
SDKGetInfo8(
	imStdParms			*stdParms, 
	imFileAccessRec8	*fileAccessInfo8, 
	imFileInfoRec8		*SDKFileInfo8)
{
	prMALError					result				= malNoError;


	SDKFileInfo8->hasDataRate						= kPrFalse;


	// private data
	assert(SDKFileInfo8->privatedata);
	ImporterLocalRec8H ldataH = reinterpret_cast<ImporterLocalRec8H>(SDKFileInfo8->privatedata);
	stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));
	ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );


	SDKFileInfo8->hasVideo = kPrFalse;
	SDKFileInfo8->hasAudio = kPrFalse;
	
	
	if(localRecP && localRecP->audio_track && localRecP->alac)
	{
		try
		{
			assert(localRecP->reader != NULL);
			assert(localRecP->file != NULL && localRecP->file->GetMovie() != NULL);
			
			assert(localRecP->audio_track->GetSampleDescriptionCount() == 1);
			
			AP4_SampleDescription *desc = localRecP->audio_track->GetSampleDescription(0);
			
			if(desc != NULL && desc->GetFormat() == AP4_SAMPLE_FORMAT_ALAC)
			{
				AP4_AudioSampleDescription *audio_desc = AP4_DYNAMIC_CAST(AP4_AudioSampleDescription, desc);
				
				if(audio_desc != NULL)
				{
					// Audio information
					SDKFileInfo8->hasAudio				= kPrTrue;
					SDKFileInfo8->audInfo.numChannels	= audio_desc->GetChannelCount();
					SDKFileInfo8->audInfo.sampleRate	= audio_desc->GetSampleRate();
					
					if(SDKFileInfo8->audInfo.sampleRate != localRecP->alac->mConfig.sampleRate)
					{
						// This appears to be a disturbing Bento4 bug.
						// Or maybe not?  The ALACs on this page had the same issue:
						// http://www.linnrecords.com/linn-downloads-testfiles.aspx
						//
						// The problem arises when the sample rate is larger than uint16_t,
						// although the field should be a uint32_t
						
						SDKFileInfo8->audInfo.sampleRate = localRecP->alac->mConfig.sampleRate;
						//assert(false); // not OK?
					}
					
					
					const AP4_UI16 bitDepth				= audio_desc->GetSampleSize();
					
					SDKFileInfo8->audInfo.sampleType	= bitDepth == 8 ? kPrAudioSampleType_8BitInt :
															bitDepth == 16 ? kPrAudioSampleType_16BitInt :
															bitDepth == 24 ? kPrAudioSampleType_24BitInt :
															bitDepth == 32 ? kPrAudioSampleType_32BitInt :
															bitDepth == 64 ? kPrAudioSampleType_64BitFloat :
															kPrAudioSampleType_Compressed;
															
					SDKFileInfo8->audDuration			= localRecP->audio_track->GetDuration() *
															SDKFileInfo8->audInfo.sampleRate /
															localRecP->audio_track->GetMediaTimeScale();
					
					
					localRecP->bitDepth = bitDepth;
					
					
					assert(SDKFileInfo8->audInfo.numChannels == localRecP->alac->mConfig.numChannels);
					assert(bitDepth == localRecP->alac->mConfig.bitDepth);
					assert(localRecP->alac->mConfig.frameLength == 4096);
				}
				else
					result = imOtherErr;
			}
			else
				result = imUnsupportedCompression;
		}
		catch(...)
		{
			result = imBadFile;
		}


		localRecP->audioSampleRate			= SDKFileInfo8->audInfo.sampleRate;
		localRecP->numChannels				= SDKFileInfo8->audInfo.numChannels;
		localRecP->duration					= SDKFileInfo8->audDuration;
		
		
		if(SDKFileInfo8->audInfo.numChannels > 2 && SDKFileInfo8->audInfo.numChannels != 6)
		{
			// Premiere can't handle anything but Mono, Stereo, and 5.1
			result = imUnsupportedAudioFormat;
		}
	}
		
	stdParms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(ldataH));

	return result;
}


template<typename INPUT>
static void
CopySamples(const INPUT *in, float **out, int channels, const int swizzle[], int samples, PrAudioSample pos, int skip)
{
	const double divisor = (1L << ((sizeof(INPUT) << 3) - 1));

	for(int c=0; c < channels; c++)
	{
		for(int i=0; i < samples; i++)
		{
			out[swizzle[c]][pos + i] = (double)in[(channels * (skip + i)) + c] / divisor;
		}
	}
}


static void
CopySamples24(const uint8_t *in, float **out, int channels, const int swizzle[], int samples, PrAudioSample pos, int skip, int bitDepth)
{
	// Apparently with ALAC, 20-bit and 24-bit audio is packed into 3 bytes.
	// Put 20/24 bit sample into 32-bits and then convert to float.
	const int bits_to_fill = 32 - bitDepth;
	const int rightshift = 31 - bits_to_fill;
	
	const double divisor = (1L << (32 - 1));
	
	
	in += 3 * channels * skip;
	

	for(int i = 0; i < samples; i++)
	{
		for(int c=0; c < channels; c++)
		{
			int32_t val = 0;
			
			uint8_t *buf = (uint8_t *)&val;
			
			// This is endian-dependant
			buf[1] = *in++;
			buf[2] = *in++;
			buf[3] = *in++;
			
			
			// fill lower bits with high bits
			// conversion to unsigned may not be necessary, but...
			uint32_t *uval = (uint32_t *)&val;
			
			*uval |= (*uval & 0x7fffffff) >> rightshift;
			
			
			out[swizzle[c]][i + pos] = (double)val / divisor;
		}
	}
}


static prMALError 
SDKImportAudio7(
	imStdParms			*stdParms, 
	imFileRef			SDKfileRef, 
	imImportAudioRec7	*audioRec7)
{
	prMALError		result		= malNoError;

	// privateData
	ImporterLocalRec8H ldataH = reinterpret_cast<ImporterLocalRec8H>(audioRec7->privateData);
	stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));
	ImporterLocalRec8Ptr localRecP = reinterpret_cast<ImporterLocalRec8Ptr>( *ldataH );


	if(localRecP && localRecP->audio_track && localRecP->alac)
	{
		assert(localRecP->reader != NULL);
		assert(localRecP->file != NULL && localRecP->file->GetMovie() != NULL);
		
		assert(audioRec7->position >= 0); // Do they really want contiguous samples?
		
		assert(audioRec7->position < localRecP->duration);
		
		if(audioRec7->size > localRecP->duration - audioRec7->position)
		{
			// this does happen, we get asked for audio data past the duration
			// let's make sure there's no garbage there and re-set audioRec7->size
			
			for(int c=0; c < localRecP->numChannels; c++)
			{
				memset(audioRec7->buffer[c], 0, sizeof(float) * audioRec7->size);
			}
			
			audioRec7->size = localRecP->duration - audioRec7->position;
		}
		
		
		const AP4_UI32 timestamp_ms = audioRec7->position * 1000 / localRecP->audioSampleRate;
		
		
		const size_t bytes_per_sample = (localRecP->alac->mConfig.bitDepth <= 16 ? 2 : 4);
		
		const size_t alac_buf_size = localRecP->alac->mConfig.frameLength * localRecP->alac->mConfig.numChannels *
										bytes_per_sample + kALACMaxEscapeHeaderBytes;
		
		uint8_t *alac_buffer = (uint8_t *)malloc(alac_buf_size);
		
		
		AP4_Ordinal sample_index = 0;
		
		AP4_Result ap4_result = localRecP->audio_track->GetSampleIndexForTimeStampMs(timestamp_ms, sample_index);
		
		if(ap4_result == AP4_SUCCESS)
		{
			// for surround channels
			// Premiere uses Left, Right, Left Rear, Right Rear, Center, LFE
			// ALAC uses Center, Left, Right, Left Rear, Right Rear, LFE
			// http://alac.macosforge.org/trac/browser/trunk/ReadMe.txt
			static const int surround_swizzle[] = {4, 0, 1, 2, 3, 5};
			static const int stereo_swizzle[] = {0, 1, 2, 3, 4, 5}; // no swizzle, actually

			const int *swizzle = localRecP->numChannels > 2 ? surround_swizzle : stereo_swizzle;
			
			
			csSDK_uint32 samples_needed = audioRec7->size;
			PrAudioSample pos = 0;
		
			AP4_DataBuffer dataBuffer;
			
			while(samples_needed > 0 && ap4_result == AP4_SUCCESS && result == malNoError)
			{
				AP4_Sample sample;
				
				ap4_result = localRecP->audio_track->ReadSample(sample_index, sample, dataBuffer);
				
				if(ap4_result == AP4_SUCCESS)
				{
					const PrAudioSample sample_pos = sample.GetDts() *
														localRecP->audioSampleRate /
														localRecP->audio_track->GetMediaTimeScale();
					
					const PrAudioSample sample_len = sample.GetDuration() *
														localRecP->audioSampleRate /
														localRecP->audio_track->GetMediaTimeScale();
					
					const PrAudioSample skip_samples = (audioRec7->position > sample_pos) ? (audioRec7->position - sample_pos) : 0;
					
					long samples_to_read = sample_len - skip_samples;
					
					if(samples_to_read > samples_needed)
						samples_to_read = samples_needed;
					else if(samples_to_read < 0)
						samples_to_read = 0;
					
					if(samples_to_read > 0)
					{
						BitBuffer bits;
						BitBufferInit(&bits, dataBuffer.UseData(), dataBuffer.GetDataSize());
		
						uint32_t outSamples = 0;
					
						int32_t alac_result = localRecP->alac->Decode(&bits,
																		alac_buffer, localRecP->alac->mConfig.frameLength, localRecP->numChannels,
																		&outSamples);
						
						if(alac_result == 0)
						{
							bool eos = false;
						
							if(samples_to_read > outSamples)
							{
								samples_to_read = outSamples;
								
								eos = true;
							}
						
							if(localRecP->alac->mConfig.bitDepth == 16)
							{
								CopySamples<int16_t>((const int16_t *)alac_buffer, audioRec7->buffer,
														localRecP->numChannels, swizzle, samples_to_read, pos, skip_samples);
							}
							else if(localRecP->alac->mConfig.bitDepth == 32)
							{
								CopySamples<int32_t>((const int32_t *)alac_buffer, audioRec7->buffer,
														localRecP->numChannels, swizzle, samples_to_read, pos, skip_samples);
							}
							else
							{
								assert(localRecP->alac->mConfig.bitDepth == 20 || localRecP->alac->mConfig.bitDepth == 24);
								
								CopySamples24(alac_buffer, audioRec7->buffer,
												localRecP->numChannels, swizzle, samples_to_read, pos, skip_samples,
												localRecP->alac->mConfig.bitDepth);
							}
							
							if(eos)
							{
								// end of the stream
								break;
							}
						}
						else
							assert(false);
					}
					
					
					samples_needed -= samples_to_read;
					pos += samples_to_read;
					
					sample_index++;
				}
				else
					assert(false);
			}
			
			
			assert(ap4_result == AP4_SUCCESS);
			
			
			if(ap4_result != AP4_SUCCESS && ap4_result != AP4_ERROR_EOS && ap4_result != AP4_ERROR_OUT_OF_RANGE)
			{
				result = imFileReadFailed;
			}
		}
		else
		{
			assert(ap4_result == AP4_ERROR_EOS);
		}
		
		free(alac_buffer);
	}
	
					
	stdParms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(ldataH));
	
	assert(result == malNoError);
	
	return result;
}


PREMPLUGENTRY DllExport xImportEntry (
	csSDK_int32		selector, 
	imStdParms		*stdParms, 
	void			*param1, 
	void			*param2)
{
	prMALError	result				= imUnsupported;

	try{

	switch (selector)
	{
		case imInit:
			result =	SDKInit(stdParms, 
								reinterpret_cast<imImportInfoRec*>(param1));
			break;

		case imGetInfo8:
			result =	SDKGetInfo8(stdParms, 
									reinterpret_cast<imFileAccessRec8*>(param1), 
									reinterpret_cast<imFileInfoRec8*>(param2));
			break;

		case imOpenFile8:
			result =	SDKOpenFile8(	stdParms, 
										reinterpret_cast<imFileRef*>(param1), 
										reinterpret_cast<imFileOpenRec8*>(param2));
			break;
		
		case imQuietFile:
			result =	SDKQuietFile(	stdParms, 
										reinterpret_cast<imFileRef*>(param1), 
										param2); 
			break;

		case imCloseFile:
			result =	SDKCloseFile(	stdParms, 
										reinterpret_cast<imFileRef*>(param1), 
										param2);
			break;

		case imAnalysis:
			result =	SDKAnalysis(	stdParms,
										reinterpret_cast<imFileRef>(param1),
										reinterpret_cast<imAnalysisRec*>(param2));
			break;

		case imGetIndFormat:
			result =	SDKGetIndFormat(stdParms, 
										reinterpret_cast<csSDK_size_t>(param1),
										reinterpret_cast<imIndFormatRec*>(param2));
			break;

		// Importers that support the Premiere Pro 2.0 API must return malSupports8 for this selector
		case imGetSupports8:
			result = malSupports8;
			break;

		case imImportAudio7:
			result =	SDKImportAudio7(	stdParms,
											reinterpret_cast<imFileRef>(param1),
											reinterpret_cast<imImportAudioRec7*>(param2));
			break;

		case imCreateAsyncImporter:
			result =	imUnsupported;
			break;
	}
	
	}catch(...) { result = imOtherErr; }

	return result;
}

