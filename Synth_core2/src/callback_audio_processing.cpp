/*
 * Copyright (c) 2018 Analog Devices, Inc.  All rights reserved.
 *
 * These are the hooks for the your audio processing functions.
 *
 */

// Define your audio system parameters in this file
#include "common/audio_system_config.h"

// Only enable these functions if we're using a dual-core framework
#if (USE_BOTH_CORES_TO_PROCESS_AUDIO)

// Support for simple multi-core data sharing
#include "common/multicore_shared_memory.h"

// Variables related to the audio framework that is currently selected (e.g. input and output buffers)
#include "audio_framework_selector.h"

// Prototypes for these functions
#include "callback_audio_processing.h"

#include "audio_processing/audio_elements/audio_utilities.h"
#include "audio_processing/audio_elements/biquad_filter.h"
#include "audio_processing/audio_elements/integer_delay_lpf.h"

/*
 *
 * Available Processing Power
 * --------------------------
 *
 * The two SHARC cores provide a hefty amount of audio processing power.  However, it is
 * important to ensure that any audio processing code can run and complete within one frame of audio.
 *
 * The total number of cycles available in the audio callback can be calculated as follows:
 *
 * total cycles = ( processor-clock-speed * audio-block-size ) / audio-sample-rate//
 *
 * For example, if the processor is running at 450MHz, the audio sampling rate is 48KHz and the
 * audio block size is set to 32 words, the total number of processor cycles available in each
 * callback is 300,000 cycles or 300,000/32 or 9,375 per sample of audio.
 *
 * Available Audio Buffers
 * -----------------------
 *
 * There are several sets of audio input and output buffers that correspond to the
 * various peripherals (e.g. audio codec, USB, S/PDIF, A2B).
 *
 * SHARC Core 1 manages the audio flow to these various peripherals.  SHARC Core 2
 * only has access to a set of input and output buffers used to move audio from
 * core 1 to core 2 and from core 2 to core 1.  If Faust is being used, SHARC Core 2
 * also has access to a send of input and output buffers for Faust.
 *
 * Inputs Buffers
 * **************
 *
 *  Audio from SHARC Core 1
 *
 *     audiochannel_0_left_in[];
 *     audiochannel_0_right_in[];
 *     audiochannel_1_left_in[];
 *     audiochannel_1_right_in[];
 *     audiochannel_2_left_in[];
 *     audiochannel_2_right_in[];
 *     audiochannel_3_left_in[];
 *     audiochannel_3_right_in[];
 *
 *     If the automotive board is being used, we have an additional 8 channels from
 *     SHARC Core 1 (e.g. audiochannel_4_left_in[]... audiochannel_7_left_in[])
 *
 *  Audio from Faust (be sure to enable Faust in audio_system_config.h)
 *
 *     audioChannel_faust_0_left_in[]
 *     audioChannel_faust_0_right_in[]
 *     audioChannel_faust_1_left_in[]
 *     audioChannel_faust_1_right_in[]
 *     audioChannel_faust_2_left_in[]
 *     audioChannel_faust_2_right_in[]
 *     audioChannel_faust_3_left_in[]
 *     audioChannel_faust_3_right_in[]
 *
 *
 * Output Buffers
 * *************
 *
 *   Audio sent back to SHARC Core 1 and then transmitted to various peripherals
 *
 *     audiochannel_0_left_out[];
 *     audiochannel_0_right_out[];
 *     audiochannel_1_left_out[];
 *     audiochannel_1_right_out[];
 *     audiochannel_2_left_out[];
 *     audiochannel_2_right_out[];
 *     audiochannel_3_left_out[];
 *     audiochannel_3_right_out[];
 *
 *     If the automotive board is being used, we have an additional 8 channels to
 *     send back to SHARC Core 1 (e.g. audiochannel_4_left_out[]... audiochannel_7_left_out[])
 *
 *  Audio to the Faust (be sure to enable Faust in audio_system_config.h)
 *
 *     audioChannel_faust_0_left_out[]
 *     audioChannel_faust_0_right_out[]
 *     audioChannel_faust_1_left_out[]
 *     audioChannel_faust_1_right_out[]
 *     audioChannel_faust_2_left_out[]
 *     audioChannel_faust_2_right_out[]
 *     audioChannel_faust_3_left_out[]
 *     audioChannel_faust_3_right_out[]
 *
 *  Note: Faust processing occurs before the audio callback so any data
 *  copied into Faust's input buffers will be available the next time
 *  the callback is called.  Similarly, Faust's output buffers contain
 *  audio that was processed before the callback.
 */

BIQUAD_FILTER lp_filter;
float pm lp_filter_coeffs[4];
//BIQUAD_FILTER hp_filter;
//float pm hp_filter_coeffs[4];
DELAY_LPF audio_delay;
float    section("seg_sdram") delay_buffer[AUDIO_SAMPLE_RATE*2];

void processaudio_setup(void) {
	filter_setup(&lp_filter,
				 BIQUAD_TYPE_LPF,
				 BIQUAD_TRANS_MED,
				 lp_filter_coeffs,
				 3000.0,  // Center frequency
				 4.0,    // Q
				 1.0,    // Gain (db)
				 AUDIO_SAMPLE_RATE);
//	filter_setup(&hp_filter,
//				 BIQUAD_TYPE_HPF,
//				 BIQUAD_TRANS_MED,
//				 hp_filter_coeffs,
//				 100.0,  // Center frequency
//				 4.0,    // Q
//				 1.0,    // Gain (db)
//				 AUDIO_SAMPLE_RATE);

	delay_setup(&audio_delay,
				delay_buffer,
				AUDIO_SAMPLE_RATE*2,
				AUDIO_SAMPLE_RATE*0.25, // set delay line initially to 0.25 seconds
				0.8,  // Feedthrough
				0.6,  // Feedback
				0.0); // Dampening coefficient (0=no dampening)
}

 /*
  * This callback is called every time we have a new audio buffer that is ready
  * for processing.  It's currently configured for in-place processing so if no
  * processing is done to the audio, it is passed through unaffected.
  *
  * See the header file for the framework you have selected in the Audio_Frameworks
  * directory for a list of the input and output buffers that are available based on
  * the framework and hardware.
  *
  * The two SHARC cores provide a hefty amount of audio processing power. However, it is important
  * to ensure that any audio processing code can run and complete within one frame of audio.
  *
  * The total number of cycles available in the audio callback can be calculated as follows:
  * total cycles = ( processor-clock-speed * audio-block-size ) / audio-sample-rate
  *
  * For example, if the processor is running at 450MHz, the audio sampling rate is 48KHz and the audio
  * block size is set to 32 words, the total number of processor cycles available in each callback
  * is 300,000 cycles or 300,000/32 or 9,375 per sample of audio
  */

// When debugging audio algorithms, helpful to comment out this pragma for more linear single stepping.
#pragma optimize_for_speed
void processaudio_callback(void) {
	float audio_temp[AUDIO_BLOCK_SIZE];
	float audio_temp2[AUDIO_BLOCK_SIZE];

	clear_buffer(audio_temp, AUDIO_BLOCK_SIZE);
	clear_buffer(audio_temp2, AUDIO_BLOCK_SIZE);

	// Run filters on incoming L/R input audio
	filter_read(&lp_filter, audiochannel_0_left_in, audio_temp, AUDIO_BLOCK_SIZE);
//	filter_read(&hp_filter, audio_temp, audio_temp2, AUDIO_BLOCK_SIZE);

	// Run filtered audio through delay lines and send to L/R/ output audio
	delay_read(&audio_delay, audio_temp, audio_temp2, AUDIO_BLOCK_SIZE);

    for (int i = 0; i < AUDIO_BLOCK_SIZE; i++) {
    	audiochannel_0_left_out[i]  = audio_temp2[i];
    	audiochannel_0_right_out[i] = audio_temp2[i];
        audiochannel_1_left_out[i]  = audiochannel_1_left_in[i];
        audiochannel_1_right_out[i] = audiochannel_1_right_in[i];
        audiochannel_2_left_out[i]  = audiochannel_2_left_in[i];
        audiochannel_2_right_out[i] = audiochannel_2_right_in[i];
        audiochannel_3_left_out[i]  = audiochannel_3_left_in[i];
        audiochannel_3_right_out[i] = audiochannel_3_right_in[i];
    }
}

/*
 * This loop function is like a thread with a low priority.  This is good place to process
 * large FFTs in the background without interrupting the audio processing callback.
 */
void processaudio_background_loop(void) {
	char val = multicore_data->midi_cc_values[4];
	if (multicore_data->midi_cc_values_prev[4] != val)
	{
		multicore_data->midi_cc_values_prev[4] = val;
		delay_modify_feedback(&audio_delay, 0.9 * (val / 128.f));
	}
	val = multicore_data->midi_cc_values[5];
	if (multicore_data->midi_cc_values_prev[5] != val)
	{
		multicore_data->midi_cc_values_prev[5] = val;
		delay_modify_length(&audio_delay, (uint32_t)((float)AUDIO_SAMPLE_RATE * (val / 128.f)));
	}
	val = multicore_data->midi_cc_values[6];
	if (multicore_data->midi_cc_values_prev[6] != val)
	{
		multicore_data->midi_cc_values_prev[6] = val;
		filter_modify_freq(&lp_filter, (3000.f * (val / 128.f)) + 100.f);
	}
//	val = multicore_data->midi_cc_values[7];
//	if (multicore_data->midi_cc_values_prev[7] != val)
//	{
//		multicore_data->midi_cc_values_prev[7] = val;
//		filter_modify_freq(&hp_filter, (3000.f * (val / 128.f)) + 100.f);
//	}
}

/*
 * This function is called if the code in the audio processing callback takes too long
 * to complete (essentially exceeding the available computational resources of this core).
 */
void processaudio_mips_overflow(void) {
}

#endif
