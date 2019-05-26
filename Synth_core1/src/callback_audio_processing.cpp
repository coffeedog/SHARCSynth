/*
 * Copyright (c) 2018 Analog Devices, Inc.  All rights reserved.
 *
 * These are the hooks for the audio processing functions.
 *
 */

#include <math.h>

// Define your audio system parameters in this file
#include "common/audio_system_config.h"

// Support for simple multi-core data sharing
#include "common/multicore_shared_memory.h"

// Variables related to the audio framework that is currently selected (e.g. input and output buffers)
#include "audio_framework_selector.h"

// Prototypes for these functions
#include "callback_audio_processing.h"

// Synth
#include "audio_processing/audio_elements/audio_elements_common.h"
#include "audio_processing/audio_elements/audio_utilities.h"
#include "audio_processing/audio_elements/simple_synth.h"

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
 * To send audio from USB out the DAC on the ADAU1761 one simply needs to copy data
 * from the USB buffers and copy them to the ADAU1761 buffer.
 *
 * for (i=0;i<AUDIO_BLOCK_SIZE;i++) {
 *   audiochannel_adau1761_0_left_out[i] = audiochannel_USB_0_left_in[i];
 *   audiochannel_adau1761_0_right_out[i] = audiochannel_USB_0_right_in[i];
 * }
 *
 * The framework ensures that audio is sample rate converted as needed (e.g S/PDIF)
 * and arrives where it needs to be on time using DMA.  It also manages the conversion
 * between fixed and floating point.
 *
 * Below is a list of the various input buffers and output buffers that are available.
 * Be sure that the corresponding peripheral has been enabled in audio_system_config.h
 *
 * Input Buffers
 * *************
 *
 *  Audio from the ADAU1761 ADCs
 *     audiochannel_adau1761_0_left_in[]
 *     audiochannel_adau1761_0_left_in[]
 *
 *  Audio from the S/PDIF receiver
 *     audiochannel_spdif_0_left_in[]
 *     audiochannel_spdif_0_right_in[]
 *
 *  Audio from USB (be sure to enable USB in audio_system_config.h)
 *     audiochannel_USB_0_left_in[]
 *     audiochannel_USB_0_right_in[]
 *
 *  Audio from A2B Bus
 *     audiochannel_a2b_0_left_in[]
 *     audiochannel_a2b_0_right_in[]
 *     audiochannel_a2b_1_left_in[]
 *     audiochannel_a2b_1_right_in[]
 *     audiochannel_a2b_2_left_in[]
 *     audiochannel_a2b_2_right_in[]
 *     audiochannel_a2b_3_left_in[]
 *     audiochannel_a2b_3_right_in[]
 *
 *
 *  Audio from Faust (be sure to enable Faust in audio_system_config.h and include the libraries)
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
 * Output Buffers
 * **************
 *  Audio to the ADAU1761 DACs
 *     audiochannel_adau1761_0_left_out[]
 *     audiochannel_adau1761_0_left_out[]
 *
 *  Audio to the S/PDIF transmitter
 *     audiochannel_spdif_0_left_out[]
 *     audiochannel_spdif_0_right_out[]
 *
 *  Audio to USB (be sure to enable USB in audio_system_config.h)
 *     audiochannel_USB_0_left_out[]
 *     audiochannel_USB_0_right_out[]
 *
 *  Audio to A2B Bus (be sure to enable A2B in audio_system_config.h)
 *     audiochannel_a2b_0_left_out[]
 *     audiochannel_a2b_0_right_out[]
 *     audiochannel_a2b_1_left_out[]
 *     audiochannel_a2b_1_right_out[]
 *     audiochannel_a2b_2_left_out[]
 *     audiochannel_a2b_2_right_out[]
 *     audiochannel_a2b_3_left_out[]
 *     audiochannel_a2b_3_right_out[]
 *
 *  Audio from Faust (be sure to enable Faust in audio_system_config.h)
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
 *
 *
 * There is also a set of buffers for sending audio to / from SHARC Core 2
 *
 *  Output to SHARC Core 2
 *     audiochannel_to_sharc_core2_0_left[]
 *     audiochannel_to_sharc_core2_0_right[]
 *     audiochannel_to_sharc_core2_1_left[]
 *     audiochannel_to_sharc_core2_1_right[]
 *     audiochannel_to_sharc_core2_2_left[]
 *     audiochannel_to_sharc_core2_2_right[]
 *     audiochannel_to_sharc_core2_3_left[]
 *     audiochannel_to_sharc_core2_3_right[]
 *
 *  Input from SHARC Core 2 (processed audio from SHARC Core 2)
 *     audiochannel_from_sharc_core2_0_left[]
 *     audiochannel_from_sharc_core2_0_right[]
 *     audiochannel_from_sharc_core2_1_left[]
 *     audiochannel_from_sharc_core2_1_right[]
 *     audiochannel_from_sharc_core2_2_left[]
 *     audiochannel_from_sharc_core2_2_right[]
 *     audiochannel_from_sharc_core2_3_left[]
 *     audiochannel_from_sharc_core2_3_right[]
 *
 * Finally, there is a set of aliased buffers that sends audio to the
 * right place.  On SHARC 1, the In[] buffers are received from the ADC
 * and the Out[] buffers are sent to either SHARC 2 (when in dual core more)
 * or to the DACs (when in single core mode).  The In[] buffers on SHARC core
 * 2 are received from SHARC core 1 and the Out[] buffers are sent to the DACs
 * (via SHARC core 1).
 *
 *     audiochannel_0_left_in[]
 *     audiochannel_0_right_in[]
 *
 *     audiochannel_1_left_out[]
 *     audiochannel_1_right_out[]
 *     audiochannel_2_left_out[]
 *     audiochannel_2_right_out[]
 *     audiochannel_3_left_out[]
 *     audiochannel_3_right_out[]
 *
 *     When the automotive board is being used, there are 16 channels of aliased
 *     buffers, not 8.  So they go up to audiochannel_7_left_in / audiochannel_7_right_in
 *     and audiochannel_7_left_out / audiochannel_7_right_out
 *
 * See the .c/.h file for the corresponding audio framework in the Audio_Frameworks
 * directory to see the buffers that are available for other frameworks (like the
 * 16 channel automotive framework).
 *
 */

/*
 * Place any initialization code here for the audio processing
 */
SIMPLE_SYNTH synth_voices[16];

void processaudio_setup(void) {
	for (int i = 0; i < 16; i++) {
		 synth_setup(&synth_voices[i],
					2000,
					2000,
					28000,
					20000,
					SYNTH_TRIANGLE,
					(float) AUDIO_SAMPLE_RATE);
	}
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
	float temp_audio[AUDIO_BLOCK_SIZE], temp_audio_accum[AUDIO_BLOCK_SIZE];

	// Quick way to zero audiochannel_0_left_out
	clear_buffer(temp_audio_accum, AUDIO_BLOCK_SIZE);

	// Scan remaining channels and synthesize when playing
	for (int i=0;i<16;i++) {
		synth_read(&synth_voices[i], temp_audio, AUDIO_BLOCK_SIZE );

		// Mix this synth voice with our accumulated audio
		mix_2x1(temp_audio, temp_audio_accum, temp_audio_accum, AUDIO_BLOCK_SIZE);
	}

	// Scale and copy the synthesized audio to our output buffers
	gain_buffer(audiochannel_0_left_out, 0.25, AUDIO_BLOCK_SIZE);
	gain_buffer(audiochannel_0_right_out, 0.25, AUDIO_BLOCK_SIZE);
}

#if (USE_BOTH_CORES_TO_PROCESS_AUDIO)

/*
 * When using a dual core configuration, SHARC Core 1 is responsible for routing the
 * processed audio from SHARC Core 2 to the various output buffers for the
 * devices connected to the SC589.  For example, in a dual core framework, SHARC Core 1
 * may pass 8 channels of audio to Core 2, and then receive 8 channels of processed audio
 * back from Core 2.  It is this routine where we route these channels to the ADAU1761,
 * the A2B bus, SPDIF, etc.
 */
#pragma optimize_for_speed
void processaudio_output_routing(void) {

    static float t = 0;

    for (int i = 0; i < AUDIO_BLOCK_SIZE; i++) {
        // Send Audio from SHARC Core 2 out to the DACs (1/8" audio out connector)
        audiochannel_adau1761_0_left_out[i]  = audiochannel_from_sharc_core2_0_left[i];
        audiochannel_adau1761_0_right_out[i] = audiochannel_from_sharc_core2_0_right[i];

        // Send audio from SHARC Core 2 to the SPDIF transmitter as well
        audiochannel_spdif_0_left_out[i]  = audiochannel_from_sharc_core2_0_left[i];
        audiochannel_spdif_0_right_out[i] = audiochannel_from_sharc_core2_0_right[i];
        #endif
    }
}
#endif

/*
 * This loop function is like a thread with a low priority.  This is good place to process
 * large FFTs in the background without interrupting the audio processing callback.
 */
void processaudio_background_loop(void) {

	// Process MIDI data
	for (int i = 0; i < 128; i ++)
	{
		if (multicore_data->midi_note[i].velocity != multicore_data->midi_note[i].velocity_prev)
		{
			multicore_data->midi_note[i].velocity_prev = multicore_data->midi_note[i].velocity;
			if (multicore_data->midi_note[i].velocity == 0)
			{
				bool found = false;
				int indx = 0;
				do {
					if (synth_voices[indx].playing && synth_voices[indx].note == i ) {
						synth_stop_note( &synth_voices[indx] );
						found = true;
					}
					indx++;
				} while (!found && indx < 16);
			}
			else
			{
				bool found = false;
				int indx = 0;
				do {
					if (!synth_voices[indx].playing) {
						synth_play_note( &synth_voices[indx], i, (float)(multicore_data->midi_note[i].velocity)*(1.0/128.0) );
						found = true;
					}
					indx++;
				} while (!found && indx < 16);
			}
		}
	}
}

/*
 * This function is called if the code in the audio processing callback takes too long
 * to complete (essentially exceeding the available computational resources of this core).
 */
void processaudio_mips_overflow(void) {
}
