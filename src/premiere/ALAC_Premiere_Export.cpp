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



#include "ALAC_Premiere_Export.h"


#include "Ap4.h"

#include "ALACBitUtilities.h"
#include "ALACEncoder.h"

#include "ALAC_Atom.h"


#ifdef PRMAC_ENV
	#include <mach/mach.h>
#else
	#include <assert.h>
	#include <time.h>
	#include <sys/timeb.h>
#endif

#include <sstream>


static const csSDK_int32 ALAC_ID = 'ALAC';
static const csSDK_int32 ALAC_Export_Class = 'ALAC';


typedef struct ExportSettings
{
	csSDK_int32					fileType;
	SPBasicSuite				*spBasic;
	PrSDKExportParamSuite		*exportParamSuite;
	PrSDKExportInfoSuite		*exportInfoSuite;
	PrSDKExportFileSuite		*exportFileSuite;
	PrSDKExportProgressSuite	*exportProgressSuite;
	PrSDKPPixCreatorSuite		*ppixCreatorSuite;
	PrSDKPPixSuite				*ppixSuite;
	PrSDKPPix2Suite				*ppix2Suite;
	PrSDKTimeSuite				*timeSuite;
	PrSDKMemoryManagerSuite		*memorySuite;
	PrSDKSequenceRenderSuite	*sequenceRenderSuite;
	PrSDKSequenceAudioSuite		*sequenceAudioSuite;
	PrSDKWindowSuite			*windowSuite;
} ExportSettings;


static void
utf16ncpy(prUTF16Char *dest, const char *src, int max_len)
{
	prUTF16Char *d = dest;
	const char *c = src;
	
	do{
		*d++ = *c;
	}while(*c++ != '\0' && --max_len);
}


static prMALError
exSDKStartup(
	exportStdParms		*stdParmsP, 
	exExporterInfoRec	*infoRecP)
{
	PrSDKAppInfoSuite *appInfoSuite = NULL;
	stdParmsP->getSPBasicSuite()->AcquireSuite(kPrSDKAppInfoSuite, kPrSDKAppInfoSuiteVersion, (const void**)&appInfoSuite);
	
	if(appInfoSuite)
	{
		int fourCC = 0;
	
		appInfoSuite->GetAppInfo(PrSDKAppInfoSuite::kAppInfo_AppFourCC, (void *)&fourCC);
	
		stdParmsP->getSPBasicSuite()->ReleaseSuite(kPrSDKAppInfoSuite, kPrSDKAppInfoSuiteVersion);
		
		// not a good idea to try to run a MediaCore exporter in AE
		if(fourCC == kAppAfterEffects)
			return exportReturn_IterateExporterDone;
	}
	
	
	infoRecP->fileType			= ALAC_ID;
	
	utf16ncpy(infoRecP->fileTypeName, "ALAC", 255);
	utf16ncpy(infoRecP->fileTypeDefaultExtension, "m4a", 255);
	
	infoRecP->classID = ALAC_Export_Class;
	
	infoRecP->exportReqIndex	= 0;
	infoRecP->wantsNoProgressBar = kPrFalse;
	infoRecP->hideInUI			= kPrFalse;
	infoRecP->doesNotSupportAudioOnly = kPrFalse;
	infoRecP->canExportVideo	= kPrFalse;
	infoRecP->canExportAudio	= kPrTrue;
	infoRecP->singleFrameOnly	= kPrFalse;
	
	infoRecP->interfaceVersion	= EXPORTMOD_VERSION;
	
	infoRecP->isCacheable		= kPrFalse;
		

	return malNoError;
}


static prMALError
exSDKBeginInstance(
	exportStdParms			*stdParmsP, 
	exExporterInstanceRec	*instanceRecP)
{
	prMALError				result				= malNoError;
	SPErr					spError				= kSPNoError;
	ExportSettings			*mySettings;
	PrSDKMemoryManagerSuite	*memorySuite;
	csSDK_int32				exportSettingsSize	= sizeof(ExportSettings);
	SPBasicSuite			*spBasic			= stdParmsP->getSPBasicSuite();
	
	if(spBasic != NULL)
	{
		spError = spBasic->AcquireSuite(
			kPrSDKMemoryManagerSuite,
			kPrSDKMemoryManagerSuiteVersion,
			const_cast<const void**>(reinterpret_cast<void**>(&memorySuite)));
			
		mySettings = reinterpret_cast<ExportSettings *>(memorySuite->NewPtrClear(exportSettingsSize));

		if(mySettings)
		{
			mySettings->fileType = instanceRecP->fileType;
		
			mySettings->spBasic		= spBasic;
			mySettings->memorySuite	= memorySuite;
			
			spError = spBasic->AcquireSuite(
				kPrSDKExportParamSuite,
				kPrSDKExportParamSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportParamSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKExportFileSuite,
				kPrSDKExportFileSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportFileSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKExportInfoSuite,
				kPrSDKExportInfoSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportInfoSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKExportProgressSuite,
				kPrSDKExportProgressSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportProgressSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKPPixCreatorSuite,
				kPrSDKPPixCreatorSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->ppixCreatorSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKPPixSuite,
				kPrSDKPPixSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->ppixSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKPPix2Suite,
				kPrSDKPPix2SuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->ppix2Suite))));
			spError = spBasic->AcquireSuite(
				kPrSDKSequenceRenderSuite,
				kPrSDKSequenceRenderSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->sequenceRenderSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKSequenceAudioSuite,
				kPrSDKSequenceAudioSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->sequenceAudioSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKTimeSuite,
				kPrSDKTimeSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->timeSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKWindowSuite,
				kPrSDKWindowSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->windowSuite))));
		}


		instanceRecP->privateData = reinterpret_cast<void*>(mySettings);
	}
	else
	{
		result = exportReturn_ErrMemory;
	}
	
	return result;
}


static prMALError
exSDKEndInstance(
	exportStdParms			*stdParmsP, 
	exExporterInstanceRec	*instanceRecP)
{
	prMALError				result		= malNoError;
	ExportSettings			*lRec		= reinterpret_cast<ExportSettings *>(instanceRecP->privateData);
	SPBasicSuite			*spBasic	= stdParmsP->getSPBasicSuite();
	PrSDKMemoryManagerSuite	*memorySuite;
	if(spBasic != NULL && lRec != NULL)
	{
		if (lRec->exportParamSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportParamSuite, kPrSDKExportParamSuiteVersion);
		}
		if (lRec->exportFileSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportFileSuite, kPrSDKExportFileSuiteVersion);
		}
		if (lRec->exportInfoSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportInfoSuite, kPrSDKExportInfoSuiteVersion);
		}
		if (lRec->exportProgressSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportProgressSuite, kPrSDKExportProgressSuiteVersion);
		}
		if (lRec->ppixCreatorSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKPPixCreatorSuite, kPrSDKPPixCreatorSuiteVersion);
		}
		if (lRec->ppixSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKPPixSuite, kPrSDKPPixSuiteVersion);
		}
		if (lRec->ppix2Suite)
		{
			result = spBasic->ReleaseSuite(kPrSDKPPix2Suite, kPrSDKPPix2SuiteVersion);
		}
		if (lRec->sequenceRenderSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKSequenceRenderSuite, kPrSDKSequenceRenderSuiteVersion);
		}
		if (lRec->sequenceAudioSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKSequenceAudioSuite, kPrSDKSequenceAudioSuiteVersion);
		}
		if (lRec->timeSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKTimeSuite, kPrSDKTimeSuiteVersion);
		}
		if (lRec->windowSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKWindowSuite, kPrSDKWindowSuiteVersion);
		}
		if (lRec->memorySuite)
		{
			memorySuite = lRec->memorySuite;
			memorySuite->PrDisposePtr(reinterpret_cast<PrMemoryPtr>(lRec));
			result = spBasic->ReleaseSuite(kPrSDKMemoryManagerSuite, kPrSDKMemoryManagerSuiteVersion);
		}
	}

	return result;
}


static prMALError
exSDKFileExtension(
	exportStdParms					*stdParmsP, 
	exQueryExportFileExtensionRec	*exportFileExtensionRecP)
{
	utf16ncpy(exportFileExtensionRecP->outFileExtension, "m4a", 255);
		
	return malNoError;
}


class MyOther_ByteStream : public AP4_ByteStream
{
  public:
	MyOther_ByteStream(PrSDKExportFileSuite *fileSuite, csSDK_uint32 fileObject);
	virtual ~MyOther_ByteStream();
	
	virtual AP4_Result ReadPartial(void *buffer, AP4_Size bytes_to_read, AP4_Size &bytes_read);
    virtual AP4_Result WritePartial(const void *buffer, AP4_Size bytes_to_write, AP4_Size &bytes_written);
	virtual AP4_Result Seek(AP4_Position position);
	virtual AP4_Result Tell(AP4_Position &position);
	virtual AP4_Result GetSize(AP4_LargeSize &size);
	
    virtual void AddReference();
    virtual void Release();

  private:
	const PrSDKExportFileSuite *_fileSuite;
	const csSDK_uint32 _fileObject;
	
	AP4_Cardinal _refCount;
};


MyOther_ByteStream::MyOther_ByteStream(PrSDKExportFileSuite *fileSuite, csSDK_uint32 fileObject) :
	_fileSuite(fileSuite),
	_fileObject(fileObject),
	_refCount(1)
{
	prSuiteError err = _fileSuite->Open(_fileObject);
	
	if(err != malNoError)
		throw err;
}


MyOther_ByteStream::~MyOther_ByteStream()
{
	prSuiteError err = _fileSuite->Close(_fileObject);
	
	assert(err == malNoError);
}


AP4_Result
MyOther_ByteStream::ReadPartial(void *buffer, AP4_Size bytes_to_read, AP4_Size &bytes_read)
{
	return AP4_ERROR_NOT_SUPPORTED;
}


AP4_Result
MyOther_ByteStream::WritePartial(const void *buffer, AP4_Size bytes_to_write, AP4_Size &bytes_written)
{
	prSuiteError err = _fileSuite->Write(_fileObject, (void *)buffer, bytes_to_write);
	
	bytes_written = ((err == malNoError) ? bytes_to_write : 0);
	
	return ((err == malNoError) ? AP4_SUCCESS : AP4_FAILURE);
}


AP4_Result
MyOther_ByteStream::Seek(AP4_Position position)
{
	prInt64 outPos = 0;

	prSuiteError err = _fileSuite->Seek(_fileObject, position, outPos, fileSeekMode_Begin);
	
	return ((err == malNoError) ? AP4_SUCCESS : AP4_FAILURE);
}


AP4_Result
MyOther_ByteStream::Tell(AP4_Position &position)
{
	prInt64 outPos = 0;

// son of a gun, fileSeekMode_End and fileSeekMode_Current are flipped inside Premiere!
#define PR_SEEK_CURRENT fileSeekMode_End

	prSuiteError err = _fileSuite->Seek(_fileObject, 0, outPos, PR_SEEK_CURRENT);
	
	position = outPos;
	
	return ((err == malNoError) ? AP4_SUCCESS : AP4_FAILURE);
}


AP4_Result
MyOther_ByteStream::GetSize(AP4_LargeSize &size)
{
	assert(false); // this isn't really getting called, is it?

	prInt64 outPos = 0;

	AP4_Position tellPos = 0;
	Tell(tellPos);

// http://en.wikipedia.org/wiki/Facepalm
#define PR_SEEK_END fileSeekMode_Current

	prSuiteError err = _fileSuite->Seek(_fileObject, 0, outPos, PR_SEEK_END);
	
	Seek(tellPos);
	
	size = outPos;
	
	return ((err == malNoError) ? AP4_SUCCESS : AP4_FAILURE);
}


void
MyOther_ByteStream::AddReference()
{
	_refCount++;
}


void
MyOther_ByteStream::Release()
{
	_refCount--;
}



static inline int
AudioClip(double in, unsigned int max_val)
{
	// My understanding with audio is that it uses the full signed range.
	// So an 8-bit sample is allowed to go from -128 to 127.  It's not
	// balanced in positive and negative, but I guess that's OK?
	return (in >= 0 ?
				(in < (max_val - 1) ? (in + 0.5) : (max_val - 1)) :
				(in > (-(int)max_val) ? (in - 0.5) : (-(int)max_val) )
			);
			
	// BTW, the need to cast max_val into an int before the - operation
	// was the source of a horrific bug I gave myself.  Sigh.
}



template<typename OUTPUT>
static void CopySamples(OUTPUT *out, float **in, int channels, int samples, PrAudioSample pos, int bitDepth)
{
	// copy Premiere audio to ALAC buffer, swizzling channels
	// Premiere uses Left, Right, Left Rear, Right Rear, Center, LFE
	// ALAC uses Center, Left, Right, Left Rear, Right Rear, LFE
	// http://www.xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-800004.3.9
	static const int stereo_swizzle[] = {0, 1, 0, 1, 0, 1};
	static const int surround_swizzle[] = {4, 0, 1, 2, 3, 5};
	
	const int *swizzle = (channels > 2 ? surround_swizzle : stereo_swizzle);
	
	const double multiplier = (1L << (bitDepth - 1));
	
	for(int c=0; c < channels; c++)
	{
		for(int i=0; i < samples; i++)
		{
			out[((i + pos) * channels) + swizzle[c]] =
									AudioClip((double)in[c][i] * multiplier, multiplier);
		}
	}
}


// Obviously, this is the value that should go in outputDesc.mFormatFlags
// Adapted from CoreAudioTypes.h
enum
{
    kTestFormatFlag_16BitSourceData    = 1,
    kTestFormatFlag_20BitSourceData    = 2,
    kTestFormatFlag_24BitSourceData    = 3,
    kTestFormatFlag_32BitSourceData    = 4
};


static prMALError
exSDKExport(
	exportStdParms	*stdParmsP,
	exDoExportRec	*exportInfoP)
{
	prMALError					result					= malNoError;
	ExportSettings				*mySettings				= reinterpret_cast<ExportSettings*>(exportInfoP->privateData);
	PrSDKExportParamSuite		*paramSuite				= mySettings->exportParamSuite;
	PrSDKExportInfoSuite		*exportInfoSuite		= mySettings->exportInfoSuite;
	PrSDKExportFileSuite		*fileSuite				= mySettings->exportFileSuite;
	PrSDKSequenceRenderSuite	*renderSuite			= mySettings->sequenceRenderSuite;
	PrSDKSequenceAudioSuite		*audioSuite				= mySettings->sequenceAudioSuite;
	PrSDKMemoryManagerSuite		*memorySuite			= mySettings->memorySuite;
	PrSDKPPixCreatorSuite		*pixCreatorSuite		= mySettings->ppixCreatorSuite;
	PrSDKPPixSuite				*pixSuite				= mySettings->ppixSuite;
	PrSDKPPix2Suite				*pix2Suite				= mySettings->ppix2Suite;

	assert(exportInfoP->exportAudio);


	PrTime ticksPerSecond = 0;
	mySettings->timeSuite->GetTicksPerSecond(&ticksPerSecond);
	
					
	csSDK_uint32 exID = exportInfoP->exporterPluginID;
	csSDK_uint32 fileType = exportInfoP->fileType;
	csSDK_int32 gIdx = 0;
	
	
	exParamValues sampleRateP, channelTypeP, sampleSizeP;
	paramSuite->GetParamValue(exID, gIdx, ADBEAudioRatePerSecond, &sampleRateP);
	paramSuite->GetParamValue(exID, gIdx, ADBEAudioNumChannels, &channelTypeP);
	paramSuite->GetParamValue(exID, gIdx, ADBEAudioSampleType, &sampleSizeP);
	
	
	
	const PrAudioChannelType audioFormat = (PrAudioChannelType)channelTypeP.value.intValue;
	const int audioChannels = (audioFormat == kPrAudioChannelType_51 ? 6 :
								audioFormat == kPrAudioChannelType_Mono ? 1 :
								2);

	try
	{
		const int frameSize = kALACDefaultFramesPerPacket;
		
		ALACEncoder alac;
		
		alac.SetFrameSize(frameSize);
		

		const size_t bytes_per_sample = (sampleSizeP.value.intValue <= 16 ? 2 : 4);

		const size_t alac_buf_size = frameSize * audioChannels * bytes_per_sample;
												
		AudioFormatDescription inputDesc;
		inputDesc.mSampleRate = sampleRateP.value.floatValue;
		inputDesc.mFormatID = kALACFormatLinearPCM;
		inputDesc.mFormatFlags = kALACFormatFlagIsSignedInteger;
		inputDesc.mBytesPerPacket = bytes_per_sample * audioChannels;
		inputDesc.mFramesPerPacket = 1;
		inputDesc.mBytesPerFrame = bytes_per_sample;
		inputDesc.mChannelsPerFrame = audioChannels;
		inputDesc.mBitsPerChannel = sampleSizeP.value.intValue;
		inputDesc.mReserved = 0;
		
		
		const int output_format = sampleSizeP.value.intValue == 16 ? kTestFormatFlag_16BitSourceData :
									sampleSizeP.value.intValue == 20 ? kTestFormatFlag_20BitSourceData :
									sampleSizeP.value.intValue == 24 ? kTestFormatFlag_24BitSourceData :
									sampleSizeP.value.intValue == 32 ? kTestFormatFlag_32BitSourceData :
									kTestFormatFlag_16BitSourceData;
		
		AudioFormatDescription outputDesc;
		outputDesc.mSampleRate = sampleRateP.value.floatValue;
		outputDesc.mFormatID = kALACFormatAppleLossless;
		outputDesc.mFormatFlags = output_format;
		outputDesc.mBytesPerPacket = bytes_per_sample * audioChannels;
		outputDesc.mFramesPerPacket = 1;
		outputDesc.mBytesPerFrame = bytes_per_sample;
		outputDesc.mChannelsPerFrame = audioChannels;
		outputDesc.mBitsPerChannel = sampleSizeP.value.intValue;
		outputDesc.mReserved = 0;
		
		int32_t alac_err = alac.InitializeEncoder(outputDesc);
		
		if(alac_err == 0)
		{
			csSDK_uint32 audioRenderID = 0;
			result = audioSuite->MakeAudioRenderer(exID,
													exportInfoP->startTime,
													audioFormat,
													kPrAudioSampleType_32BitFloat,
													sampleRateP.value.floatValue, 
													&audioRenderID);
			if(result == malNoError)
			{
				AP4_DataBuffer magic_cookie;
				
				magic_cookie.SetDataSize(alac.GetMagicCookieSize(audioChannels));
				
				uint32_t cookie_size = magic_cookie.GetBufferSize();
				
				alac.GetMagicCookie(magic_cookie.UseData(), &cookie_size);
				
				
				ALAC_Atom *alac_atom = new ALAC_Atom(magic_cookie.UseData(), cookie_size);
				
				AP4_AtomParent *details = new AP4_AtomParent;
				
				details->AddChild(alac_atom);
				
				AP4_GenericAudioSampleDescription *sample_description = new AP4_GenericAudioSampleDescription(
																				AP4_ATOM_TYPE_ALAC,
																				(int)sampleRateP.value.floatValue << 16, // *
																				sampleSizeP.value.intValue,
																				audioChannels,
																				details);
																				
				// * A weird bug here in Bento4, it appears.  Only the high bits for sample rate are written,
				// so I have to send the bottom two bits up two spots.  This then means that no sample
				// rate over 65535 can be recorded.  AP4_MpegAudioSampleDescription::ToAtom() seems to be using
				// this work-around as well, but feels more like a bug.
				
				AP4_SyntheticSampleTable *sample_table = new AP4_SyntheticSampleTable;
				
				AP4_Result add_desc_err = sample_table->AddSampleDescription(sample_description);
				
				
				
				uint8_t *alac_buffer = (uint8_t *)malloc(alac_buf_size);
				uint8_t *alac_compressed_buffer = (uint8_t *)malloc(alac_buf_size);

			
				const csSDK_int32 maxBlip = sampleRateP.value.floatValue / 100;
				//mySettings->sequenceAudioSuite->GetMaxBlip(audioRenderID, frameRateP.value.timeValue, &maxBlip);
				
				assert(maxBlip < frameSize);
				
				float *pr_buffers[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
				
				for(int i=0; i < audioChannels; i++)
				{
					pr_buffers[i] = (float *)malloc(sizeof(float) * maxBlip);
				}
				
				const PrTime pr_duration = exportInfoP->endTime - exportInfoP->startTime;
				const long long total_samples = (PrTime)sampleRateP.value.floatValue * pr_duration / ticksPerSecond;
				long long samples_left = total_samples;
				
				
				while(samples_left > 0 && result == malNoError)
				{
					int samples_this_frame = frameSize;
					
					if(samples_this_frame > samples_left)
						samples_this_frame = samples_left;
					
					int samples_left_this_frame = samples_this_frame;
					int pos_this_frame = 0;
					
					while(samples_left_this_frame > 0 && result == malNoError)
					{
						int samples_to_get = maxBlip;
						
						if(samples_to_get > samples_left_this_frame)
							samples_to_get = samples_left_this_frame;
						
						// first fill up the frame
						result = audioSuite->GetAudio(audioRenderID, samples_to_get, pr_buffers, true);
						
						if(result == malNoError)
						{
							if(sampleSizeP.value.intValue == 16)
							{
								CopySamples<int16_t>((int16_t *)alac_buffer, pr_buffers,
														audioChannels, samples_to_get, pos_this_frame,
														sampleSizeP.value.intValue);
							}
						}
						
						samples_left_this_frame -= samples_to_get;
						pos_this_frame += samples_to_get;
						
						samples_left -= samples_to_get;
					}
					
					
					
					int32_t compressed_bytes = alac_buf_size;
					
					int32_t alac_err = alac.Encode(inputDesc, outputDesc, alac_buffer, alac_compressed_buffer, &compressed_bytes);
					
					if(alac_err == 0)
					{
						AP4_MemoryByteStream *sample_data = new AP4_MemoryByteStream(alac_compressed_buffer, compressed_bytes);
						
						sample_table->AddSample(*sample_data, 0, compressed_bytes, samples_this_frame, 0, 0, 0, true);
						
						sample_data->Release();
					}
					else
						result = exportReturn_ErrCodecBadInput;
					
					
					if(result == malNoError)
					{
						float progress = (double)(total_samples - samples_left) / (double)total_samples;
						
						result = mySettings->exportProgressSuite->UpdateProgressPercent(exID, progress);
						
						if(result == suiteError_ExporterSuspended)
						{
							result = mySettings->exportProgressSuite->WaitForResume(exID);
						}
					}
				}
				
				
				AP4_Track *track = new AP4_Track(AP4_Track::TYPE_AUDIO,
													sample_table,
													0,
													sampleRateP.value.floatValue,
													total_samples,
													sampleRateP.value.floatValue,
													total_samples,
													"eng",
													0, 0);
				
				AP4_Movie *movie = new AP4_Movie;
				
				movie->AddTrack(track);
				
				
				AP4_File file(movie);
				
				AP4_UI32 compatible_brands[2] = {
					AP4_FILE_BRAND_ISOM,
					AP4_FILE_BRAND_MP42
				};
				file.SetFileType(AP4_FILE_BRAND_M4A_, 0, compatible_brands, 2);
				
				
				
				MyOther_ByteStream writer(fileSuite, exportInfoP->fileObject);
		
				
				AP4_Result write_result = AP4_FileWriter::Write(file, writer);
				
				if(write_result != AP4_SUCCESS)
				{
					if(write_result == AP4_ERROR_OUT_OF_MEMORY)
						result = exportReturn_ErrMemory;
					else if(write_result == AP4_ERROR_PERMISSION_DENIED)
						result = exportReturn_ErrPermErr;
					else if(write_result == AP4_ERROR_NOT_ENOUGH_SPACE)
						result = exportReturn_OutOfDiskSpace;
					else if(write_result == AP4_ERROR_WRITE_FAILED)
						result = exportReturn_ErrIo;
					else
						result = exportReturn_InternalError;
				}
				
				
				for(int i=0; i < audioChannels; i++)
				{
					if(pr_buffers[i] != NULL)
						free(pr_buffers[i]);
				}
				
				free(alac_buffer);
				free(alac_compressed_buffer);
			}
		}
	}
	catch(...)
	{
		result = exportReturn_InternalError;
	}
	
/*	if(fileType == Ogg_ID)
	{
		exParamValues audioMethodP, audioQualityP, audioBitrateP;
		paramSuite->GetParamValue(exID, gIdx, OggAudioMethod, &audioMethodP);
		paramSuite->GetParamValue(exID, gIdx, OggAudioQuality, &audioQualityP);
		paramSuite->GetParamValue(exID, gIdx, OggAudioBitrate, &audioBitrateP);
		
	
		int v_err = OV_OK;

		vorbis_info vi;
		vorbis_info_init(&vi);
		
		if(audioMethodP.value.intValue == OGG_BITRATE)
		{
			v_err = vorbis_encode_init(&vi,
										audioChannels,
										sampleRateP.value.floatValue,
										-1,
										audioBitrateP.value.intValue * 1000,
										-1);
		}
		else
		{
			v_err = vorbis_encode_init_vbr(&vi,
											audioChannels,
											sampleRateP.value.floatValue,
											audioQualityP.value.floatValue);
		}
		
		if(v_err == OV_OK)
		{
			result = fileSuite->Open(exportInfoP->fileObject);
			
			if(result == malNoError)
			{
				csSDK_uint32 audioRenderID = 0;
				result = audioSuite->MakeAudioRenderer(exID,
														exportInfoP->startTime,
														audioFormat,
														kPrAudioSampleType_32BitFloat,
														sampleRateP.value.floatValue, 
														&audioRenderID);
				if(result == malNoError)
				{
					vorbis_comment vc;
					vorbis_dsp_state vd;
					vorbis_block vb;

					vorbis_comment_init(&vc);
					vorbis_analysis_init(&vd, &vi);
					vorbis_block_init(&vd, &vb);
					
					ogg_stream_state os;
					srand(time(NULL));
					ogg_stream_init(&os, rand());
					
					ogg_packet id_header;
					ogg_packet header_comm;
					ogg_packet header_code;
					
					vorbis_analysis_headerout(&vd, &vc, &id_header, &header_comm, &header_code);
					
					ogg_stream_packetin(&os, &id_header);
					ogg_stream_packetin(&os, &header_comm);
					ogg_stream_packetin(&os, &header_code);
			
			
					ogg_page og;
					
					while( ogg_stream_flush(&os, &og) )
					{
						fileSuite->Write(exportInfoP->fileObject, og.header, og.header_len);
						fileSuite->Write(exportInfoP->fileObject, og.body, og.body_len);
					}
					
					
					
					// How am I supposed to know the frame rate for maxBlip?  This is audio-only.
					// How about this....
					const csSDK_int32 maxBlip = sampleRateP.value.floatValue / 100;
					//mySettings->sequenceAudioSuite->GetMaxBlip(audioRenderID, frameRateP.value.timeValue, &maxBlip);
					
					float *temp_buffer[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
					
					if(audioChannels > 2)
					{
						for(int i=0; i < audioChannels; i++)
						{
							temp_buffer[i] = (float *)memorySuite->NewPtr(sizeof(float) * maxBlip);
						}
					}
					
					const PrTime pr_duration = exportInfoP->endTime - exportInfoP->startTime;
					const long long total_samples = (PrTime)sampleRateP.value.floatValue * pr_duration / ticksPerSecond;
					long long samples_to_get = total_samples;
					
					while(samples_to_get >= 0 && result == malNoError)
					{
						int samples = samples_to_get;
						
						if(samples > maxBlip)
							samples = maxBlip;
						
						if(samples > 0)
						{
							float **buffer = vorbis_analysis_buffer(&vd, samples);
							
							float **prbuffer = audioChannels > 2 ? temp_buffer : buffer;
							
							result = audioSuite->GetAudio(audioRenderID, samples, prbuffer, false);
							
							if(audioChannels > 2)
							{
								// copy Premiere audio to Vorbis buffer, swizzling channels
								// Premiere uses Left, Right, Left Rear, Right Rear, Center, LFE
								// Ogg uses Left, Center, Right, Left Read, Right Rear, LFE
								// http://www.xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-800004.3.9
								static const int swizzle[] = {0, 4, 1, 2, 3, 5};
								
								for(int c=0; c < audioChannels; c++)
								{
									for(int i=0; i < samples; i++)
									{
										buffer[swizzle[c]][i] = prbuffer[c][i];
									}
								}
							}
						}
							
						if(result == malNoError)
						{
							vorbis_analysis_wrote(&vd, samples);
					
							while( vorbis_analysis_blockout(&vd, &vb) )
							{
								vorbis_analysis(&vb, NULL);
								vorbis_bitrate_addblock(&vb);

								ogg_packet op;
								
								while( vorbis_bitrate_flushpacket(&vd, &op) )
								{
									ogg_stream_packetin(&os, &op);
									
									while( ogg_stream_pageout(&os, &og) )
									{
										fileSuite->Write(exportInfoP->fileObject, og.header, og.header_len);
										fileSuite->Write(exportInfoP->fileObject, og.body, og.body_len);
									}
								}
							}
						}
						
						// this way there's one last call to vorbis_analysis_wrote(&vd, 0);
						if(samples > 0)
							samples_to_get -= samples;
						else
							samples_to_get = -1;
						
						
						if(result == malNoError)
						{
							float progress = (double)(total_samples - samples_to_get) / (double)total_samples;
							
							result = mySettings->exportProgressSuite->UpdateProgressPercent(exID, progress);
							
							if(result == suiteError_ExporterSuspended)
							{
								result = mySettings->exportProgressSuite->WaitForResume(exID);
							}
						}
					}
					
					for(int i=0; i < audioChannels; i++)
					{
						if(temp_buffer[i] != NULL)
							memorySuite->PrDisposePtr((PrMemoryPtr)temp_buffer[i]);
					}
					
					ogg_stream_clear(&os);
					vorbis_block_clear(&vb);
					vorbis_dsp_clear(&vd);
					vorbis_comment_clear(&vc);
					vorbis_info_clear(&vi);
					
					audioSuite->ReleaseAudioRenderer(exID, audioRenderID);
				}
				
				fileSuite->Close(exportInfoP->fileObject);
			}
		}
		else
			result = exportReturn_InternalError;
	}
	else if(fileType == Opus_ID)
	{
		exParamValues autoBitrateP, audioBitrateP;
		paramSuite->GetParamValue(exID, gIdx, OpusAudioAutoBitrate, &autoBitrateP);
		paramSuite->GetParamValue(exID, gIdx, OpusAudioBitrate, &audioBitrateP);
		
	
		const int sample_rate = 48000;
		
		const int mapping_family = (audioChannels > 2 ? 1 : 0);
		
		const int streams = (audioChannels > 2 ? 4 : 1);
		const int coupled_streams = (audioChannels > 2 ? 2 : 1);
		
		const unsigned char surround_mapping[6] = {0, 4, 1, 2, 3, 5};
		const unsigned char stereo_mapping[6] = {0, 1, 0, 1, 0, 1};
		
		const unsigned char *mapping = (audioChannels > 2 ? surround_mapping : stereo_mapping);
		
		int err = -1;
		
		OpusMSEncoder *enc = opus_multistream_encoder_create(sample_rate, audioChannels,
															streams, coupled_streams, mapping,
															OPUS_APPLICATION_AUDIO, &err);
		
		if(enc != NULL && err == OPUS_OK)
		{
			if(!autoBitrateP.value.intValue) // OPUS_AUTO is the default
				opus_multistream_encoder_ctl(enc, OPUS_SET_BITRATE(audioBitrateP.value.intValue * 1000));
					
		
			result = fileSuite->Open(exportInfoP->fileObject);
			
			if(result == malNoError)
			{
				csSDK_uint32 audioRenderID = 0;
				result = audioSuite->MakeAudioRenderer(exID,
														exportInfoP->startTime,
														audioFormat,
														kPrAudioSampleType_32BitFloat,
														sample_rate, 
														&audioRenderID);
				if(result == malNoError)
				{
					// build Opus headers
					// http://wiki.xiph.org/OggOpus
					// http://tools.ietf.org/html/draft-terriberry-oggopus-01
					
					// ID header
					unsigned char id_head[28];
					memset(id_head, 0, 28);
					size_t id_header_size = 0;
					
					strcpy((char *)id_head, "OpusHead");
					id_head[8] = 1; // version
					id_head[9] = audioChannels;
					
					
					// pre-skip
					opus_int32 skip = 0;
					opus_multistream_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&skip));
					
					const unsigned short skip_us = skip;
					id_head[10] = skip_us & 0xff;
					id_head[11] = skip_us >> 8;
					
					
					// sample rate
					const unsigned int sample_rate_ui = sample_rate;
					id_head[12] = sample_rate_ui & 0xff;
					id_head[13] = (sample_rate_ui & 0xff00) >> 8;
					id_head[14] = (sample_rate_ui & 0xff0000) >> 16;
					id_head[15] = (sample_rate_ui & 0xff000000) >> 24;
					
					
					// output gain (set to 0)
					id_head[16] = id_head[17] = 0;
					
					
					// channel mapping
					id_head[18] = mapping_family;
					
					if(mapping_family == 1)
					{
						assert(audioChannels == 6);
					
						id_head[19] = streams;
						id_head[20] = coupled_streams;
						memcpy(&id_head[21], mapping, 6);
						
						id_header_size = 27;
					}
					else
					{
						id_header_size = 19;
					}
					
					
					ogg_stream_state os;
					srand(time(NULL));
					ogg_stream_init(&os, rand());
					
					ogg_int64_t ogg_granule_pos = 0;
					ogg_int64_t ogg_packet_num = 0;
					
					
					ogg_packet id_header;
					
					id_header.packet = id_head;
					id_header.bytes = id_header_size;
					id_header.b_o_s = 1;
					id_header.e_o_s = 0;
					id_header.granulepos = 0;
					id_header.packetno = ogg_packet_num++;
					
					ogg_stream_packetin(&os, &id_header);
					
					
					// Comment header
					unsigned char comment_head[32];
					memset(comment_head, 0, 32);
					
					strcpy((char *)comment_head, "OpusTags");
					
					unsigned int vendor_string_len = 8; // strlen("AdobeOgg") == 8 
					comment_head[8] = vendor_string_len & 0xff;
					comment_head[9] = (vendor_string_len & 0xff00) >> 8;
					comment_head[10] = (vendor_string_len & 0xff0000) >> 16;
					comment_head[11] = (vendor_string_len & 0xff000000) >> 24;
					
					strcpy((char *)&comment_head[12], "AdobeOgg");
					
					unsigned int list_len = 0;
					comment_head[20] = list_len & 0xff;
					comment_head[21] = (list_len & 0xff00) >> 8;
					comment_head[22] = (list_len & 0xff0000) >> 16;
					comment_head[23] = (list_len & 0xff000000) >> 24;
					
					
					ogg_packet comment_header;
					
					comment_header.packet = comment_head;
					comment_header.bytes = 24;
					comment_header.b_o_s = 0;
					comment_header.e_o_s = 0;
					comment_header.granulepos = 0;
					comment_header.packetno = ogg_packet_num++;
					
					ogg_stream_packetin(&os, &comment_header);
					
					
					// write headers
					ogg_page og;
					
					while( ogg_stream_flush(&os, &og) )
					{
						fileSuite->Write(exportInfoP->fileObject, og.header, og.header_len);
						fileSuite->Write(exportInfoP->fileObject, og.body, og.body_len);
					}
					
					
					// time to encode
					
					const csSDK_int32 maxBlip = sample_rate / 50; // must end up being 120, 240, 480, 960, 1920, or 2880 for 48kHz
					
					const PrTime pr_duration = exportInfoP->endTime - exportInfoP->startTime;
					const long long total_samples = (PrTime)sample_rate * pr_duration / ticksPerSecond;
					long long samples_to_get = total_samples;
					
					
					float *pr_buffer[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
					
					for(int i=0; i < audioChannels; i++)
					{
						pr_buffer[i] = (float *)memorySuite->NewPtr(sizeof(float) * maxBlip);
					}
					
					// stereo (interleaved) buffer
					const size_t stereo_buffer_size = audioChannels * maxBlip * sizeof(float);
					float *stereo_buffer = (float *)memorySuite->NewPtr(stereo_buffer_size);
					
					// opus buffer
					const size_t opus_buffer_size = 2 * stereo_buffer_size; // heck, make it twice as big as uncompressed
					unsigned char *opus_buffer = (unsigned char *)memorySuite->NewPtr(opus_buffer_size);
					
					while(samples_to_get > 0 && result == malNoError)
					{
						int samples = samples_to_get;
						
						if(samples > maxBlip)
							samples = maxBlip;
						else
							memset(stereo_buffer, 0, stereo_buffer_size); // zero out buffer
						
						if(samples > 0)
						{
							result = audioSuite->GetAudio(audioRenderID, samples, pr_buffer, false);
						
							// copy Premiere audio to Opus buffer, swizzling channels
							// Premiere uses Left, Right, Left Rear, Right Rear, Center, LFE
							// Opus uses Left, Center, Right, Left Read, Right Rear, LFE
							// http://www.xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-800004.3.9
							static const int stereo_swizzle[] = {0, 1, 0, 1, 0, 1};
							static const int surround_swizzle[] = {0, 4, 1, 2, 3, 5};
							
							const int *swizzle = (audioChannels > 2 ? surround_swizzle : stereo_swizzle);
							
							for(int c=0; c < audioChannels; c++)
							{
								for(int i=0; i < samples; i++)
								{
									stereo_buffer[(i * audioChannels) + swizzle[c]] = pr_buffer[c][i];
								}
							}
						}
							
						if(result == malNoError)
						{
							opus_int32 packet_size = opus_multistream_encode_float(enc, stereo_buffer, maxBlip, opus_buffer, opus_buffer_size);

							if(packet_size > 0)
							{
								assert(opus_packet_get_samples_per_frame(opus_buffer, sample_rate) == maxBlip);
								assert(opus_packet_get_nb_frames(opus_buffer, packet_size) == 1);
								
								ogg_granule_pos += samples;
								
								
								ogg_packet op;
								
								op.packet = opus_buffer;
								op.bytes = packet_size;
								op.b_o_s = 0;
								op.e_o_s = (samples == samples_to_get ? 1 : 0);
								op.granulepos = ogg_granule_pos;
								op.packetno = ogg_packet_num++;
								
								ogg_stream_packetin(&os, &op);
								
								
								while( ogg_stream_flush(&os, &og) )
								{
									fileSuite->Write(exportInfoP->fileObject, og.header, og.header_len);
									fileSuite->Write(exportInfoP->fileObject, og.body, og.body_len);
								}
							}
							else
								result = exportReturn_InternalError;
						}
						
						if(result == malNoError)
						{
							float progress = (double)(total_samples - samples_to_get) / (double)total_samples;
							
							result = mySettings->exportProgressSuite->UpdateProgressPercent(exID, progress);
							
							if(result == suiteError_ExporterSuspended)
							{
								result = mySettings->exportProgressSuite->WaitForResume(exID);
							}
						}
						
						samples_to_get -= samples;
					}
					
					
					memorySuite->PrDisposePtr((PrMemoryPtr)stereo_buffer);
					memorySuite->PrDisposePtr((PrMemoryPtr)opus_buffer);
					
					for(int i=0; i < audioChannels; i++)
					{
						if(pr_buffer[i] != NULL)
							memorySuite->PrDisposePtr((PrMemoryPtr)pr_buffer[i]);
					}

						
					ogg_stream_clear(&os);
					
				
					audioSuite->ReleaseAudioRenderer(exID, audioRenderID);
				}
				
				fileSuite->Close(exportInfoP->fileObject);
			}
			
			opus_multistream_encoder_destroy(enc);
		}
	}
	else if(fileType == FLAC_ID)
	{
		exParamValues sampleSizeP, FLACcompressionP;
		paramSuite->GetParamValue(exID, gIdx, ADBEAudioSampleType, &sampleSizeP);
		paramSuite->GetParamValue(exID, gIdx, FLACAudioCompression, &FLACcompressionP);
		
		
		const csSDK_int32 maxBlip = sampleRateP.value.floatValue / 100;
		
		const PrTime pr_duration = exportInfoP->endTime - exportInfoP->startTime;
		const long long total_samples = (PrTime)sampleRateP.value.floatValue * pr_duration / ticksPerSecond;
		
		csSDK_uint32 audioRenderID = 0;
		result = audioSuite->MakeAudioRenderer(exID,
												exportInfoP->startTime,
												audioFormat,
												kPrAudioSampleType_32BitFloat,
												sampleRateP.value.floatValue, 
												&audioRenderID);
		if(result == malNoError)
		{
			try
			{
				OurEncoder encoder(fileSuite, exportInfoP->fileObject, mySettings->exportProgressSuite, exID);
				
				encoder.set_verify(true);
				encoder.set_compression_level(FLACcompressionP.value.intValue);
				encoder.set_channels(audioChannels);
				encoder.set_bits_per_sample(sampleSizeP.value.intValue);
				encoder.set_sample_rate(sampleRateP.value.floatValue);
				encoder.set_total_samples_estimate(total_samples);
				
				
				FLAC__StreamMetadata_VorbisComment_Entry entry;
				FLAC__StreamMetadata *tag_it = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
				FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&entry, "Writer", "fnord Ogg/FLAC for Premiere");
				encoder.set_metadata(&tag_it, 1);
				
				
				FLAC__StreamEncoderInitStatus status = encoder.init();
				
				if(status == FLAC__STREAM_ENCODER_INIT_STATUS_OK)
				{
					float *float_buffers[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
					FLAC__int32 *int_buffers[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
					
					for(int c=0; c < audioChannels; c++)
					{
						float_buffers[c] = (float *)memorySuite->NewPtr(maxBlip * sizeof(float));
						int_buffers[c] = (FLAC__int32 *)memorySuite->NewPtr(maxBlip * sizeof(FLAC__int32));
					}
					
					
					long long samples = total_samples;
					
					while(samples > 0 && result == malNoError)
					{
						int samples_to_get = maxBlip;
						
						if(samples_to_get > samples)
							samples_to_get = samples;
					
						result = audioSuite->GetAudio(audioRenderID, samples_to_get, float_buffers, true);
						
						if(result == malNoError)
						{
							const double multiplier = (1L << (sampleSizeP.value.intValue - 1));
							
							for(int c=0; c < audioChannels; c++)
							{
								// for surround channels
								// Premiere uses Left, Right, Left Rear, Right Rear, Center, LFE
								// FLAC uses Left, Right, Center, LFE, Left Rear, Right Rear
								// http://xiph.org/flac/format.html#frame_header
								static const int swizzle[] = {0, 1, 4, 5, 2, 3};
								
								for(int i=0; i < samples_to_get; i++)
								{
									int_buffers[swizzle[c]][i] = AudioClip((double)float_buffers[c][i] * multiplier, multiplier);
								}
							}
							
							bool ok = encoder.process(int_buffers, samples_to_get);
							
							samples -= samples_to_get;
							
							if(!ok)
								result = exportReturn_InternalError;
						}
						
						
						if(result == malNoError)
						{
							float progress = (double)(total_samples - samples) / (double)total_samples;
							
							result = mySettings->exportProgressSuite->UpdateProgressPercent(exID, progress);
							
							if(result == suiteError_ExporterSuspended)
							{
								result = mySettings->exportProgressSuite->WaitForResume(exID);
							}
						}
					}
					
					
					bool ok = encoder.finish();
					
					assert(ok);
					
					
					for(int c=0; c < audioChannels; c++)
					{
						memorySuite->PrDisposePtr((PrMemoryPtr)float_buffers[c]);
						memorySuite->PrDisposePtr((PrMemoryPtr)int_buffers[c]);
					}
				}
				else
					result = exportReturn_IncompatibleAudioChannelType;
				
				
				FLAC__metadata_object_delete(tag_it);
			}
			catch(...)
			{
				result = exportReturn_InternalError;
			}
			
			audioSuite->ReleaseAudioRenderer(exID, audioRenderID);
		}
	}*/

	return result;
}


prMALError
exSDKQueryOutputSettings(
	exportStdParms				*stdParmsP,
	exQueryOutputSettingsRec	*outputSettingsP)
{
	prMALError result = malNoError;
	
	ExportSettings *privateData	= reinterpret_cast<ExportSettings*>(outputSettingsP->privateData);
	
	csSDK_uint32				exID			= outputSettingsP->exporterPluginID;
	csSDK_uint32				fileType		= outputSettingsP->fileType;
	exParamValues				sampleRate,
								channelType;
	PrSDKExportParamSuite		*paramSuite		= privateData->exportParamSuite;
	csSDK_int32					gIdx			= 0;
	float						fps				= 0.0f;
	PrTime						ticksPerSecond	= 0;
	csSDK_uint32				videoBitrate	= 0;
	
	privateData->timeSuite->GetTicksPerSecond(&ticksPerSecond);
	
	
	if(outputSettingsP->inExportAudio)
	{
		paramSuite->GetParamValue(exID, gIdx, ADBEAudioRatePerSecond, &sampleRate);
		outputSettingsP->outAudioSampleRate = sampleRate.value.floatValue;
		paramSuite->GetParamValue(exID, gIdx, ADBEAudioNumChannels, &channelType);
		outputSettingsP->outAudioChannelType = (PrAudioChannelType)channelType.value.intValue;
		outputSettingsP->outAudioSampleType = kPrAudioSampleType_Compressed;
		
		const PrAudioChannelType audioFormat = (PrAudioChannelType)channelType.value.intValue;
		const int audioChannels = (audioFormat == kPrAudioChannelType_51 ? 6 :
									audioFormat == kPrAudioChannelType_Mono ? 1 :
									2);

		exParamValues sampleSizeP;
		paramSuite->GetParamValue(exID, gIdx, ADBEAudioSampleType, &sampleSizeP);
		
		const float flac_mult = 0.5;
	
		videoBitrate += (flac_mult * sampleRate.value.floatValue * audioChannels * sampleSizeP.value.intValue) / 1024; // IDK
	}
	
	// return outBitratePerSecond in kbps
	outputSettingsP->outBitratePerSecond = videoBitrate;


	return result;
}


prMALError
exSDKGenerateDefaultParams(
	exportStdParms				*stdParms, 
	exGenerateDefaultParamRec	*generateDefaultParamRec)
{
	prMALError				result				= malNoError;

	ExportSettings			*lRec				= reinterpret_cast<ExportSettings *>(generateDefaultParamRec->privateData);
	PrSDKExportParamSuite	*exportParamSuite	= lRec->exportParamSuite;
	PrSDKExportInfoSuite	*exportInfoSuite	= lRec->exportInfoSuite;
	PrSDKTimeSuite			*timeSuite			= lRec->timeSuite;

	csSDK_int32 exID = generateDefaultParamRec->exporterPluginID;
	csSDK_uint32 fileType = generateDefaultParamRec->fileType;
	csSDK_int32 gIdx = 0;
	
	prUTF16Char groupString[256];
	
	// get current settings
	PrParam channelsTypeP, sampleRateP;
	
	exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_AudioChannelsType, &channelsTypeP);
	exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_AudioSampleRate, &sampleRateP);
	
	
	// Multi Group
	exportParamSuite->AddMultiGroup(exID, &gIdx);
	
	
	// Audio Tab
	utf16ncpy(groupString, "Audio Tab", 255);
	exportParamSuite->AddParamGroup(exID, gIdx,
									ADBETopParamGroup, ADBEAudioTabGroup, groupString,
									kPrFalse, kPrFalse, kPrFalse);


	// Audio Settings group
	utf16ncpy(groupString, "Audio Settings", 255);
	exportParamSuite->AddParamGroup(exID, gIdx,
									ADBEAudioTabGroup, ADBEBasicAudioGroup, groupString,
									kPrFalse, kPrFalse, kPrFalse);
	
	// Sample rate
	exParamValues sampleRateValues;
	sampleRateValues.value.floatValue = sampleRateP.mFloat64;
	sampleRateValues.disabled = kPrFalse;
	sampleRateValues.hidden = kPrFalse;
		
	exNewParamInfo sampleRateParam;
	sampleRateParam.structVersion = 1;
	strncpy(sampleRateParam.identifier, ADBEAudioRatePerSecond, 255);
	sampleRateParam.paramType = exParamType_float;
	sampleRateParam.flags = exParamFlag_none;
	sampleRateParam.paramValues = sampleRateValues;
	
	exportParamSuite->AddParam(exID, gIdx, ADBEBasicAudioGroup, &sampleRateParam);
	
	
	// Channel type
	exParamValues channelTypeValues;
	channelTypeValues.value.intValue = channelsTypeP.mInt32;
	channelTypeValues.disabled = kPrFalse;
	channelTypeValues.hidden = kPrFalse;
	
	exNewParamInfo channelTypeParam;
	channelTypeParam.structVersion = 1;
	strncpy(channelTypeParam.identifier, ADBEAudioNumChannels, 255);
	channelTypeParam.paramType = exParamType_int;
	channelTypeParam.flags = exParamFlag_none;
	channelTypeParam.paramValues = channelTypeValues;
	
	exportParamSuite->AddParam(exID, gIdx, ADBEBasicAudioGroup, &channelTypeParam);
	
	
	// Sample size (bit depth)
	exParamValues audioSampleSizeValues;
	audioSampleSizeValues.structVersion = 1;
	audioSampleSizeValues.rangeMin.intValue = 8;
	audioSampleSizeValues.rangeMax.intValue = 32;
	audioSampleSizeValues.value.intValue = 16;
	audioSampleSizeValues.disabled = kPrFalse;
	audioSampleSizeValues.hidden = kPrFalse;
	
	exNewParamInfo audioSampleSizeParam;
	audioSampleSizeParam.structVersion = 1;
	strncpy(audioSampleSizeParam.identifier, ADBEAudioSampleType, 255);
	audioSampleSizeParam.paramType = exParamType_int;
	audioSampleSizeParam.flags = exParamFlag_none;
	audioSampleSizeParam.paramValues = audioSampleSizeValues;
	
	exportParamSuite->AddParam(exID, gIdx, ADBEBasicAudioGroup, &audioSampleSizeParam);
	
	

	exportParamSuite->SetParamsVersion(exID, 1);
	
	
	return result;
}


prMALError
exSDKPostProcessParams(
	exportStdParms			*stdParmsP, 
	exPostProcessParamsRec	*postProcessParamsRecP)
{
	prMALError		result	= malNoError;

	ExportSettings			*lRec				= reinterpret_cast<ExportSettings *>(postProcessParamsRecP->privateData);
	PrSDKExportParamSuite	*exportParamSuite	= lRec->exportParamSuite;
	//PrSDKExportInfoSuite	*exportInfoSuite	= lRec->exportInfoSuite;
	PrSDKTimeSuite			*timeSuite			= lRec->timeSuite;

	csSDK_int32 exID = postProcessParamsRecP->exporterPluginID;
	csSDK_int32 fileType = postProcessParamsRecP->fileType;
	csSDK_int32 gIdx = 0;
	
	prUTF16Char paramString[256];
	
	
	// Audio Settings group
	utf16ncpy(paramString, "Audio Settings", 255);
	exportParamSuite->SetParamName(exID, gIdx, ADBEBasicAudioGroup, paramString);
	
	
	// Sample rate
	utf16ncpy(paramString, "Sample Rate", 255);
	exportParamSuite->SetParamName(exID, gIdx, ADBEAudioRatePerSecond, paramString);
	
	float sampleRates[] = { 8000.f,
							11025.f,
							16000.f,
							22050.f,
							32000.f,
							44100.f,
							48000.f,
							88200.f,
							96000.f };
	
	const char *sampleRateStrings[] = { "8000 Hz",
										"11025 Hz",
										"16000 Hz",
										"22050 Hz",
										"32000 Hz",
										"44100 Hz",
										"48000 Hz",
										"88200 Hz",
										"96000 Hz" };
	
	
	exportParamSuite->ClearConstrainedValues(exID, gIdx, ADBEAudioRatePerSecond);
	
	exOneParamValueRec tempSampleRate;
	
	for(csSDK_int32 i=0; i < sizeof(sampleRates) / sizeof(float); i++)
	{
		tempSampleRate.floatValue = sampleRates[i];
		utf16ncpy(paramString, sampleRateStrings[i], 255);
		exportParamSuite->AddConstrainedValuePair(exID, gIdx, ADBEAudioRatePerSecond, &tempSampleRate, paramString);
	}

	
	// Channels
	utf16ncpy(paramString, "Channels", 255);
	exportParamSuite->SetParamName(exID, gIdx, ADBEAudioNumChannels, paramString);
	
	csSDK_int32 channelTypes[] = { kPrAudioChannelType_Mono,
									kPrAudioChannelType_Stereo,
									kPrAudioChannelType_51 };
	
	const char *channelTypeStrings[] = { "Mono", "Stereo", "Dolby 5.1" };
	
	
	exportParamSuite->ClearConstrainedValues(exID, gIdx, ADBEAudioNumChannels);
	
	exOneParamValueRec tempChannelType;
	
	for(csSDK_int32 i=0; i < sizeof(channelTypes) / sizeof(csSDK_int32); i++)
	{
		tempChannelType.intValue = channelTypes[i];
		utf16ncpy(paramString, channelTypeStrings[i], 255);
		exportParamSuite->AddConstrainedValuePair(exID, gIdx, ADBEAudioNumChannels, &tempChannelType, paramString);
	}
	

	// Sample Size
	utf16ncpy(paramString, "Sample Size", 255);
	exportParamSuite->SetParamName(exID, gIdx, ADBEAudioSampleType, paramString);
	
	int sampleSizes[] = { 16, 20, 24, 32 };
	
	const char *sampleSizeStrings[] = { "16-bit", "20-bit", "24-bit", "32-bit" };
	
	
	exportParamSuite->ClearConstrainedValues(exID, gIdx, ADBEAudioSampleType);
	
	exOneParamValueRec tempSampleType;
	
	for(csSDK_int32 i=0; i < 4; i++)
	{
		tempSampleType.intValue = sampleSizes[i];
		utf16ncpy(paramString, sampleSizeStrings[i], 255);
		exportParamSuite->AddConstrainedValuePair(exID, gIdx, ADBEAudioSampleType, &tempSampleType, paramString);
	}

	
	return result;
}


prMALError
exSDKGetParamSummary(
	exportStdParms			*stdParmsP, 
	exParamSummaryRec		*summaryRecP)
{
	ExportSettings			*privateData	= reinterpret_cast<ExportSettings*>(summaryRecP->privateData);
	PrSDKExportParamSuite	*paramSuite		= privateData->exportParamSuite;
	
	std::string summary1, summary2, summary3;

	csSDK_uint32	exID	= summaryRecP->exporterPluginID;
	csSDK_uint32	fileType = privateData->fileType;
	csSDK_int32		gIdx	= 0;
	
	// Standard settings
	exParamValues sampleRateP, channelTypeP, sampleSizeP;
	paramSuite->GetParamValue(exID, gIdx, ADBEAudioRatePerSecond, &sampleRateP);
	paramSuite->GetParamValue(exID, gIdx, ADBEAudioNumChannels, &channelTypeP);
	paramSuite->GetParamValue(exID, gIdx, ADBEAudioSampleType, &sampleSizeP);


	std::stringstream stream1;
	
	stream1 << "stream1";
	
	std::stringstream stream2;
	
	stream2 << (int)sampleRateP.value.floatValue << " Hz";
	stream2 << ", " << (channelTypeP.value.intValue == kPrAudioChannelType_51 ? "Dolby 5.1" :
						channelTypeP.value.intValue == kPrAudioChannelType_Mono ? "Mono" :
						"Stereo");
	stream2 << ", ";
	
	
	stream2 << sampleSizeP.value.intValue << "-bit";
	
	summary2 = stream2.str();
	
	
	std::stringstream stream3;
	
	stream3 << "stream3";
	

	utf16ncpy(summaryRecP->Summary1, summary1.c_str(), 255);
	utf16ncpy(summaryRecP->Summary2, summary2.c_str(), 255);
	utf16ncpy(summaryRecP->Summary3, summary3.c_str(), 255);
	
	return malNoError;
}


prMALError
exSDKValidateParamChanged (
	exportStdParms		*stdParmsP, 
	exParamChangedRec	*validateParamChangedRecP)
{
	ExportSettings			*privateData	= reinterpret_cast<ExportSettings*>(validateParamChangedRecP->privateData);
	PrSDKExportParamSuite	*paramSuite		= privateData->exportParamSuite;
	
	csSDK_int32 exID = validateParamChangedRecP->exporterPluginID;
	csSDK_int32 fileType = validateParamChangedRecP->fileType;
	csSDK_int32 gIdx = validateParamChangedRecP->multiGroupIndex;
	
	const std::string param = validateParamChangedRecP->changedParamIdentifier;
	

	return malNoError;
}


DllExport PREMPLUGENTRY xSDKExport (
	csSDK_int32		selector, 
	exportStdParms	*stdParmsP, 
	void			*param1, 
	void			*param2)
{
	prMALError result = exportReturn_Unsupported;
	
	switch (selector)
	{
		case exSelStartup:
			result = exSDKStartup(	stdParmsP, 
									reinterpret_cast<exExporterInfoRec*>(param1));
			break;

		case exSelBeginInstance:
			result = exSDKBeginInstance(stdParmsP,
										reinterpret_cast<exExporterInstanceRec*>(param1));
			break;

		case exSelEndInstance:
			result = exSDKEndInstance(	stdParmsP,
										reinterpret_cast<exExporterInstanceRec*>(param1));
			break;

		case exSelGenerateDefaultParams:
			result = exSDKGenerateDefaultParams(stdParmsP,
												reinterpret_cast<exGenerateDefaultParamRec*>(param1));
			break;

		case exSelPostProcessParams:
			result = exSDKPostProcessParams(stdParmsP,
											reinterpret_cast<exPostProcessParamsRec*>(param1));
			break;

		case exSelGetParamSummary:
			result = exSDKGetParamSummary(	stdParmsP,
											reinterpret_cast<exParamSummaryRec*>(param1));
			break;

		case exSelQueryOutputSettings:
			result = exSDKQueryOutputSettings(	stdParmsP,
												reinterpret_cast<exQueryOutputSettingsRec*>(param1));
			break;

		case exSelQueryExportFileExtension:
			result = exSDKFileExtension(stdParmsP,
										reinterpret_cast<exQueryExportFileExtensionRec*>(param1));
			break;

		case exSelValidateParamChanged:
			result = exSDKValidateParamChanged(	stdParmsP,
												reinterpret_cast<exParamChangedRec*>(param1));
			break;

		case exSelValidateOutputSettings:
			result = malNoError;
			break;

		case exSelExport:
			result = exSDKExport(	stdParmsP,
									reinterpret_cast<exDoExportRec*>(param1));
			break;
	}
	
	return result;
}
