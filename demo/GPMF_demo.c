/*! @file GPMF_demo.c
 *
 *  @brief Demo to extract GPMF from an MP4
 *
 *  @version 1.0.1
 *
 *  (C) Copyright 2017 GoPro Inc (http://gopro.com/).
 *	
 *  Licensed under either:
 *  - Apache License, Version 2.0, http://www.apache.org/licenses/LICENSE-2.0  
 *  - MIT license, http://opensource.org/licenses/MIT
 *  at your option.
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "../GPMF_parser.h"
#include "GPMF_mp4reader.h"


extern void PrintGPMF(GPMF_stream *ms);



float getExposure(uint32_t *shut, uint32_t *isog)
{
	uint32_t valS, valG;
	float *fvalS = (float *)&valS;
	float *fvalG = (float *)&valG;
	valS = BYTESWAP32(*shut);
	valG = BYTESWAP32(*isog);

	return *fvalS * *fvalG;
}

int main(int argc, char *argv[])
{
	int32_t ret = GPMF_OK;
	GPMF_stream metadata_stream, *ms = &metadata_stream;
	GPMF_stream metadata_streamB, *msB = &metadata_streamB;
	double metadatalengthA;
	double metadatalengthB;
	uint32_t *payloadA = NULL; //buffer to store GPMF samples from the MP4.
	uint32_t *payloadB = NULL; //buffer to store GPMF samples from the MP4.
	float maxexposurediff = 0.0f;
	float limitexposurediff = -1.0f;


	// get file return data
	if (argc < 3)
	{
		printf("usage: gpmf-expose fusion_front.mp4 fusion_back.mp4 -<switches> \n");
		printf("          -s<max>  Maximum exposure difference in stops e.g. -s0 or -s1.5\n");
		return -1;
	}

	if (argc >= 4 && argv[3][0] == '-' && argv[3][1] == 's')
		limitexposurediff = (float)atof(&argv[3][2]);

	size_t mp4a = OpenMP4Source(argv[1], MOV_GPMF_TRAK_TYPE, MOV_GPMF_TRAK_SUBTYPE);
	size_t mp4b = OpenMP4Source(argv[2], MOV_GPMF_TRAK_TYPE, MOV_GPMF_TRAK_SUBTYPE);

	metadatalengthA = GetDuration(mp4a);
	metadatalengthB = GetDuration(mp4b);

	if (metadatalengthA > 0.0 && metadatalengthB > 0.0 && metadatalengthA < metadatalengthB * 1.2f && metadatalengthA * 1.2f > metadatalengthB)
	{
		uint32_t index, payloads = GetNumberPayloads(mp4a);
		uint32_t payloadsB = GetNumberPayloads(mp4b);

		if(payloads > payloadsB)
			payloads = payloadsB; // use the lower of the two.

		for (index = 0; index < payloads; index++)
		{
			uint32_t payloadsizeA = GetPayloadSize(mp4a, index);
			uint32_t payloadsizeB = GetPayloadSize(mp4b, index);
			float in = 0.0, out = 0.0; //times
			payloadA = GetPayload(mp4a, payloadA, index);
			if (payloadA == NULL)
				goto cleanup;

			payloadB = GetPayload(mp4b, payloadB, index);
			if (payloadB == NULL)
				goto cleanup;

			ret = GetPayloadTime(mp4a, index, &in, &out);
			if (ret != GPMF_OK)
				goto cleanup;

			ret = GPMF_Init(ms, payloadA, payloadsizeA);
			if (ret != GPMF_OK)
				goto cleanup;
			ret = GPMF_Init(msB, payloadB, payloadsizeB);
			if (ret != GPMF_OK)
				goto cleanup;

			GPMF_stream shutA, isogA;
			GPMF_stream shutB, isogB;

			GPMF_CopyState(ms, &shutA);
			GPMF_CopyState(ms, &isogA);
			GPMF_CopyState(msB, &shutB);
			GPMF_CopyState(msB, &isogB);

			if (GPMF_OK == GPMF_FindNext(&shutA, STR2FOURCC("SHUT"), GPMF_RECURSE_LEVELS) &&
				GPMF_OK == GPMF_FindNext(&shutB, STR2FOURCC("SHUT"), GPMF_RECURSE_LEVELS) &&
				GPMF_OK == GPMF_FindNext(&isogA, STR2FOURCC("ISOG"), GPMF_RECURSE_LEVELS) &&
				GPMF_OK == GPMF_FindNext(&isogB, STR2FOURCC("ISOG"), GPMF_RECURSE_LEVELS))
			{
				uint32_t *data_shutA = (uint32_t *)GPMF_RawData(&shutA);
				uint32_t *data_shutB = (uint32_t *)GPMF_RawData(&shutB);
				uint32_t *data_isogA = (uint32_t *)GPMF_RawData(&isogA);
				uint32_t *data_isogB = (uint32_t *)GPMF_RawData(&isogB);

				uint32_t shut_samples = GPMF_PayloadSampleCount(&shutA);
				uint32_t shut_samplesB = GPMF_PayloadSampleCount(&shutB);
				uint32_t isog_samples = GPMF_PayloadSampleCount(&isogA);
				uint32_t isog_samplesB = GPMF_PayloadSampleCount(&isogB);
				uint32_t samples = shut_samples;

				if (samples > shut_samplesB)
					samples = shut_samplesB; // use the lower of the two.
				if (samples > isog_samples)
					samples = isog_samples; // use the lower of the two.
				if (samples > isog_samplesB)
					samples = isog_samplesB; // use the lower of the two.

				if (samples)
				{
					for (uint32_t i = 0; i < samples; i++)
					{
						float expA = getExposure(data_shutA, data_isogA);
						float expB = getExposure(data_shutB, data_isogB);

						//printf("expsore diff %f %f = %f stops\n", expA, expB, );

						float diff = fabsf(logf(expB / expA) / logf(2.0f));
						if (diff > maxexposurediff)
							maxexposurediff = diff;

						data_shutA++;
						data_shutB++;
						data_isogA++;
						data_isogB++;

					}
				}

				if (limitexposurediff >= 0.0 && maxexposurediff > limitexposurediff)
				{
					data_shutA = (uint32_t *)GPMF_RawData(&shutA);
					data_shutB = (uint32_t *)GPMF_RawData(&shutB);
					data_isogA = (uint32_t *)GPMF_RawData(&isogA);
					data_isogB = (uint32_t *)GPMF_RawData(&isogB);

					float maxscale = powf(2.0f, limitexposurediff);
					uint32_t samples = shut_samples;
					float expA, expB;

					for (uint32_t i = 0; i < samples; i++)
					{
						if (i < shut_samples && i < isog_samples)
							expA = getExposure(data_shutA, data_isogA);
						if (i < shut_samplesB && i < isog_samplesB)
							expB = getExposure(data_shutB, data_isogB);

						float diffA = fabsf(expB / expA);
						float diffB = fabsf(expA / expB);
						float scale = 1.0;

						if (diffA > maxscale)
							scale = diffA / maxscale;
						if (diffB > maxscale)
							scale = diffB / maxscale;

						uint32_t valA, valB;
						float *fvalA = (float *)&valA;
						float *fvalB = (float *)&valB;
						valA = BYTESWAP32(*data_shutA);
						valB = BYTESWAP32(*data_shutB);

						if (*fvalA >= *fvalB)
							*fvalA /= scale;
						else if (*fvalB > *fvalA)
							*fvalB /= scale;

						*data_shutA = BYTESWAP32(valA);
						*data_shutB = BYTESWAP32(valB);

						if (i < shut_samples && i < isog_samples)
							data_shutA++;
						if (i < shut_samplesB && i < isog_samplesB)
							data_shutB++;

					}

					SavePayload(mp4a, payloadA, index);
					SavePayload(mp4b, payloadB, index);
				}
			}
		}

		if (maxexposurediff)
			printf("maximum exposure difference: %2.1f stops\n", maxexposurediff);
		if (limitexposurediff >= 0.0 && maxexposurediff > limitexposurediff)
			printf("exposure difference limited to %2.1f stops\n", limitexposurediff);

	cleanup:
		if (payloadA) FreePayload(payloadA); payloadA = NULL;
		if (payloadB) FreePayload(payloadB); payloadB = NULL;
		CloseSource(mp4a);
		CloseSource(mp4b);
	}

	return ret;
}
