/*
 * Copyright (c) 2018 Analog Devices, Inc.  All rights reserved.
 *
 * These are the hooks for the MIDI / Serial processing functions.
 *
 */

// Define your audio system parameters in this file
#include "common/audio_system_config.h"

#include "common/multicore_shared_memory.h"

/**
 * UART / MIDI messages can be processed either by the ARM core or by SHARC Core 1.
 * Select which option in the audio_system_config.h file.
 */
#if defined(MIDI_UART_MANAGED_BY_ARM_CORE) && MIDI_UART_MANAGED_BY_ARM_CORE

// UART functionality for MIDI driver on Audio Project Fin
#include "drivers/bm_uart_driver/bm_uart.h"
// Simple event logging / error handling functionality
#include "drivers/bm_event_logging_driver/bm_event_logging.h"

#include "callback_midi_message.h"

// Create an instance of our MIDI UART driver
BM_UART midi_uart_arm;

/**
 * @brief Sets up MIDI on the ARM
 *
 * @return true if successful
 */
bool midi_setup_arm(void) {

    if (uart_initialize(&midi_uart_arm,
                        UART_BAUD_RATE_MIDI,
                        UART_SERIAL_8N1,
                        UART_AUDIOPROJ_DEVICE_MIDI)
        != UART_SUCCESS) {
        return false;
    }

    // Set our user call back for received MIDI bytes
    uart_set_rx_callback(&midi_uart_arm, midi_rx_callback_arm);
    return true;
}

/**
 * @brief Callback when new MIDI bytes arrive
 */
void midi_rx_callback_arm(void) {

    uint8_t val;
    uint8_t status = 0;
    uint8_t channel = 0;
    uint8_t note_num = 0;
    uint8_t command_num = 0;

    // Keep reading bytes from MIDI FIFO until we have processed all of them
    while (uart_available(&midi_uart_arm)) {

        // Replace the uart_read_byte() / uart_write_byte() functions below with any custom code
        // This code just passes the received MIDI byte back to MIDI out

        // Read the new byte
        uart_read_byte(&midi_uart_arm, &val);

        if (val & 0x80) // Status byte
        {
        	channel = (val & 0x0F);
        	status = (val & 0xF0);
        	command_num = 0;
        }
        else
        {
        	switch (status)
        	{
        		case 0x80: // Note off
        			if (command_num == 0)
        			{
        				multicore_data->midi_note_velocities[val] = 0;
        			}
        			break;
        		case 0x90: // Note on
        			if (command_num == 0)
					{
        				note_num = val;
					}
        			else if (command_num == 1)
        			{
						multicore_data->midi_note_velocities[note_num] = val;
					}
        			break;
        		case 0xB0: // CC
        			if (command_num == 0)
					{
						note_num = val;
					}
					else if (command_num == 1)
					{
						multicore_data->midi_cc_values[note_num] = val;
					}
        			break;
        	}
        	command_num ++;
        }

        // Write that byte back to MIDI TX
        //uart_write_byte(&midi_uart_arm, val);
    }
}

#endif
