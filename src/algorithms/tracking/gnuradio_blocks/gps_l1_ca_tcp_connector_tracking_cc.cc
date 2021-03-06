/*!
 * \file gps_l1_ca_tcp_connector_tracking_cc.cc
 * \brief Implementation of a TCP connector block based on Code DLL + carrier PLL
 * \author David Pubill, 2012. dpubill(at)cttc.es
 *         Javier Arribas, 2011. jarribas(at)cttc.es
 *
 *
 * Code DLL + carrier PLL according to the algorithms described in:
 * [1] K.Borre, D.M.Akos, N.Bertelsen, P.Rinder, and S.H.Jensen,
 * A Software-Defined GPS and Galileo Receiver. A Single-Frequency
 * Approach, Birkha user, 2007
 *
 * -------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2014  (see AUTHORS file for a list of contributors)
 *
 * GNSS-SDR is a software defined Global Navigation
 *          Satellite Systems receiver
 *
 * This file is part of GNSS-SDR.
 *
 * GNSS-SDR is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * at your option) any later version.
 *
 * GNSS-SDR is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNSS-SDR. If not, see <http://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------------
 */

#include "gps_l1_ca_tcp_connector_tracking_cc.h"
#include <cmath>
#include <iostream>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <gnuradio/io_signature.h>
#include <glog/logging.h>
#include "gnss_synchro.h"
#include "gps_sdr_signal_processing.h"
#include "tracking_discriminators.h"
#include "lock_detectors.h"
#include "GPS_L1_CA.h"
#include "control_message_factory.h"
#include "tcp_communication.h"
#include "tcp_packet_data.h"

/*!
 * \todo Include in definition header file
 */
#define CN0_ESTIMATION_SAMPLES 20
#define MINIMUM_VALID_CN0 25
#define MAXIMUM_LOCK_FAIL_COUNTER 50
#define CARRIER_LOCK_THRESHOLD 0.85

using google::LogMessage;

gps_l1_ca_tcp_connector_tracking_cc_sptr
gps_l1_ca_tcp_connector_make_tracking_cc(
        long if_freq,
        long fs_in,
        unsigned int vector_length,
        boost::shared_ptr<gr::msg_queue> queue,
        bool dump,
        std::string dump_filename,
        float pll_bw_hz,
        float dll_bw_hz,
        float early_late_space_chips,
        size_t port_ch0)
{
    return gps_l1_ca_tcp_connector_tracking_cc_sptr(new Gps_L1_Ca_Tcp_Connector_Tracking_cc(if_freq,
            fs_in, vector_length, queue, dump, dump_filename, pll_bw_hz, dll_bw_hz, early_late_space_chips, port_ch0));
}



void Gps_L1_Ca_Tcp_Connector_Tracking_cc::forecast (int noutput_items,
        gr_vector_int &ninput_items_required)
{
    ninput_items_required[0] = (int)d_vector_length*2; //set the required available samples in each call
}



Gps_L1_Ca_Tcp_Connector_Tracking_cc::Gps_L1_Ca_Tcp_Connector_Tracking_cc(
        long if_freq,
        long fs_in,
        unsigned int vector_length,
        boost::shared_ptr<gr::msg_queue> queue,
        bool dump,
        std::string dump_filename,
        float pll_bw_hz,
        float dll_bw_hz,
        float early_late_space_chips,
        size_t port_ch0) :
        gr::block("Gps_L1_Ca_Tcp_Connector_Tracking_cc", gr::io_signature::make(1, 1, sizeof(gr_complex)),
                gr::io_signature::make(1, 1, sizeof(Gnss_Synchro)))
{
    // initialize internal vars
    d_queue = queue;
    d_dump = dump;
    d_if_freq = if_freq;
    d_fs_in = fs_in;
    d_vector_length = vector_length;
    d_dump_filename = dump_filename;

    // Initialize tracking  ==========================================
    d_code_loop_filter.set_DLL_BW(dll_bw_hz);
    d_carrier_loop_filter.set_PLL_BW(pll_bw_hz);

    //--- DLL variables --------------------------------------------------------
    d_early_late_spc_chips = early_late_space_chips; // Define early-late offset (in chips)

    //--- TCP CONNECTOR variables --------------------------------------------------------
    d_port_ch0 = port_ch0;
    d_port = 0;
    d_listen_connection = true;
    d_control_id = 0;

    // Initialization of local code replica
    // Get space for a vector with the C/A code replica sampled 1x/chip
    d_ca_code = new gr_complex[(int)GPS_L1_CA_CODE_LENGTH_CHIPS + 2];
    d_carr_sign = new gr_complex[d_vector_length*2];

    /* If an array is partitioned for more than one thread to operate on,
     * having the sub-array boundaries unaligned to cache lines could lead
     * to performance degradation. Here we allocate memory
     * (gr_comlex array of size 2*d_vector_length) aligned to cache of 16 bytes
     */
    // todo: do something if posix_memalign fails
    // Get space for the resampled early / prompt / late local replicas
    if (posix_memalign((void**)&d_early_code, 16, d_vector_length * sizeof(gr_complex) * 2) == 0){};
    if (posix_memalign((void**)&d_late_code, 16, d_vector_length * sizeof(gr_complex) * 2) == 0){};
    if (posix_memalign((void**)&d_prompt_code, 16, d_vector_length * sizeof(gr_complex) * 2) == 0){};
    // space for carrier wipeoff and signal baseband vectors
    if (posix_memalign((void**)&d_carr_sign, 16, d_vector_length * sizeof(gr_complex) * 2) == 0){};
    // correlator outputs (scalar)
    if (posix_memalign((void**)&d_Early, 16, sizeof(gr_complex)) == 0){};
    if (posix_memalign((void**)&d_Prompt, 16, sizeof(gr_complex)) == 0){};
    if (posix_memalign((void**)&d_Late, 16, sizeof(gr_complex)) == 0){};

    //--- Perform initializations ------------------------------
    // define initial code frequency basis of NCO
    d_code_freq_hz = GPS_L1_CA_CODE_RATE_HZ;
    // define residual code phase (in chips)
    d_rem_code_phase_samples = 0.0;
    // define residual carrier phase
    d_rem_carr_phase_rad = 0.0;

    // sample synchronization
    d_sample_counter = 0;
    d_sample_counter_seconds = 0;
    d_acq_sample_stamp = 0;

    d_enable_tracking = false;
    d_pull_in = false;
    d_last_seg = 0;

    d_current_prn_length_samples = (int)d_vector_length;

    // CN0 estimation and lock detector buffers
    d_cn0_estimation_counter = 0;
    d_Prompt_buffer = new gr_complex[CN0_ESTIMATION_SAMPLES];
    d_carrier_lock_test = 1;
    d_CN0_SNV_dB_Hz = 0;
    d_carrier_lock_fail_counter = 0;
    d_carrier_lock_threshold = CARRIER_LOCK_THRESHOLD;

    systemName["G"] = std::string("GPS");
    systemName["R"] = std::string("GLONASS");
    systemName["S"] = std::string("SBAS");
    systemName["E"] = std::string("Galileo");
    systemName["C"] = std::string("Compass");
}

void Gps_L1_Ca_Tcp_Connector_Tracking_cc::start_tracking()
{
    /*
     *  correct the code phase according to the delay between acq and trk
     */

    d_acq_code_phase_samples = d_acquisition_gnss_synchro->Acq_delay_samples;
    d_acq_carrier_doppler_hz = d_acquisition_gnss_synchro->Acq_doppler_hz;
    d_acq_sample_stamp =  d_acquisition_gnss_synchro->Acq_samplestamp_samples;

    long int acq_trk_diff_samples;
    float acq_trk_diff_seconds;
    // jarribas: this patch correct a situation where the tracking sample counter
    // is equal to 0 (remains in the initial state) at the first acquisition to tracking transition
    // of the receiver operation when is connecting to simulink server.
    //    if (d_sample_counter<d_acq_sample_stamp)
    //    {
    //    	acq_trk_diff_samples=0; //disable the correction
    //    }else{
    //    	acq_trk_diff_samples = d_sample_counter - d_acq_sample_stamp;//-d_vector_length;
    //    }
    acq_trk_diff_samples = (long int)d_sample_counter - (long int)d_acq_sample_stamp;
    std::cout << "acq_trk_diff_samples=" << acq_trk_diff_samples << std::endl;
    acq_trk_diff_seconds = (float)acq_trk_diff_samples / (float)d_fs_in;
    //doppler effect
    // Fd=(C/(C+Vr))*F
    float radial_velocity;
    radial_velocity = (GPS_L1_FREQ_HZ + d_acq_carrier_doppler_hz)/GPS_L1_FREQ_HZ;
    // new chip and prn sequence periods based on acq Doppler
    float T_chip_mod_seconds;
    float T_prn_mod_seconds;
    float T_prn_mod_samples;
    d_code_freq_hz = radial_velocity * GPS_L1_CA_CODE_RATE_HZ;
    T_chip_mod_seconds = 1/d_code_freq_hz;
    T_prn_mod_seconds = T_chip_mod_seconds * GPS_L1_CA_CODE_LENGTH_CHIPS;
    T_prn_mod_samples = T_prn_mod_seconds * (float)d_fs_in;

    d_next_prn_length_samples = round(T_prn_mod_samples);

    float T_prn_true_seconds = GPS_L1_CA_CODE_LENGTH_CHIPS / GPS_L1_CA_CODE_RATE_HZ;
    float T_prn_true_samples = T_prn_true_seconds * (float)d_fs_in;
    float T_prn_diff_seconds;
    T_prn_diff_seconds = T_prn_true_seconds - T_prn_mod_seconds;
    float N_prn_diff;
    N_prn_diff = acq_trk_diff_seconds / T_prn_true_seconds;
    float corrected_acq_phase_samples, delay_correction_samples;
    corrected_acq_phase_samples = fmod((d_acq_code_phase_samples + T_prn_diff_seconds * N_prn_diff * (float)d_fs_in), T_prn_true_samples);
    if (corrected_acq_phase_samples < 0)
        {
            corrected_acq_phase_samples = T_prn_mod_samples + corrected_acq_phase_samples;
        }
    delay_correction_samples = d_acq_code_phase_samples - corrected_acq_phase_samples;

    d_acq_code_phase_samples = corrected_acq_phase_samples;

    d_carrier_doppler_hz = d_acq_carrier_doppler_hz;
    // DLL/PLL filter initialization
    d_carrier_loop_filter.initialize(); //initialize the carrier filter
    d_code_loop_filter.initialize(); //initialize the code filter

    // generate local reference ALWAYS starting at chip 1 (1 sample per chip)
    gps_l1_ca_code_gen_complex(&d_ca_code[1], d_acquisition_gnss_synchro->PRN, 0);
    d_ca_code[0] = d_ca_code[(int)GPS_L1_CA_CODE_LENGTH_CHIPS];
    d_ca_code[(int)GPS_L1_CA_CODE_LENGTH_CHIPS + 1] = d_ca_code[1];

    d_carrier_lock_fail_counter = 0;
    d_rem_code_phase_samples = 0;
    d_rem_carr_phase_rad = 0;
    d_rem_code_phase_samples = 0;
    d_next_rem_code_phase_samples = 0;
    d_acc_carrier_phase_rad = 0;

    d_code_phase_samples = d_acq_code_phase_samples;

    std::string sys_ = &d_acquisition_gnss_synchro->System;
    sys = sys_.substr(0,1);

    // DEBUG OUTPUT
    std::cout << "Tracking start on channel " << d_channel << " for satellite " << Gnss_Satellite(systemName[sys], d_acquisition_gnss_synchro->PRN) << std::endl;
    LOG(INFO) << "Starting tracking of satellite " << Gnss_Satellite(systemName[sys], d_acquisition_gnss_synchro->PRN) << " on channel " << d_channel;

    // enable tracking
    d_pull_in = true;
    d_enable_tracking = true;

    LOG(INFO) << "PULL-IN Doppler [Hz]=" << d_carrier_doppler_hz
            << " Code Phase correction [samples]=" << delay_correction_samples
            << " PULL-IN Code Phase [samples]=" << d_acq_code_phase_samples;
}





void Gps_L1_Ca_Tcp_Connector_Tracking_cc::update_local_code()
{
    double tcode_chips;
    double rem_code_phase_chips;
    int associated_chip_index;
    int code_length_chips = (int)GPS_L1_CA_CODE_LENGTH_CHIPS;
    double code_phase_step_chips;
    int early_late_spc_samples;
    int epl_loop_length_samples;

    // unified loop for E, P, L code vectors
    code_phase_step_chips = ((double)d_code_freq_hz) / ((double)d_fs_in);
    rem_code_phase_chips = d_rem_code_phase_samples * (d_code_freq_hz / d_fs_in);
    tcode_chips = -rem_code_phase_chips;

    // Alternative EPL code generation (40% of speed improvement!)
    early_late_spc_samples = round(d_early_late_spc_chips/code_phase_step_chips);
    epl_loop_length_samples = d_current_prn_length_samples+early_late_spc_samples*2;
    for (int i = 0; i < epl_loop_length_samples; i++)
        {
            associated_chip_index = 1 + round(fmod(tcode_chips - d_early_late_spc_chips, code_length_chips));
            d_early_code[i] = d_ca_code[associated_chip_index];
            tcode_chips = tcode_chips + d_code_phase_step_chips;
        }

    memcpy(d_prompt_code,&d_early_code[early_late_spc_samples], d_current_prn_length_samples* sizeof(gr_complex));
    memcpy(d_late_code,&d_early_code[early_late_spc_samples*2], d_current_prn_length_samples* sizeof(gr_complex));
}




void Gps_L1_Ca_Tcp_Connector_Tracking_cc::update_local_carrier()
{
    float phase_rad, phase_step_rad;

    phase_step_rad = (float)GPS_TWO_PI*d_carrier_doppler_hz / (float)d_fs_in;
    phase_rad = d_rem_carr_phase_rad;
    for(int i = 0; i < d_current_prn_length_samples; i++)
        {
            d_carr_sign[i] = gr_complex(cos(phase_rad), -sin(phase_rad));
            phase_rad += phase_step_rad;
        }
    d_rem_carr_phase_rad = fmod(phase_rad, GPS_TWO_PI);
    d_acc_carrier_phase_rad = d_acc_carrier_phase_rad + d_rem_carr_phase_rad;
}




Gps_L1_Ca_Tcp_Connector_Tracking_cc::~Gps_L1_Ca_Tcp_Connector_Tracking_cc()
{
    d_dump_file.close();

    free(d_prompt_code);
    free(d_late_code);
    free(d_early_code);
    free(d_carr_sign);
    free(d_Early);
    free(d_Prompt);
    free(d_Late);

    delete[] d_ca_code;
    delete[] d_Prompt_buffer;

    d_tcp_com.close_tcp_connection(d_port);
}




int Gps_L1_Ca_Tcp_Connector_Tracking_cc::general_work (int noutput_items, gr_vector_int &ninput_items,
        gr_vector_const_void_star &input_items, gr_vector_void_star &output_items)
{
    // process vars
    float carr_error;
    float carr_nco;
    float code_error;
    float code_nco;

    tcp_packet_data tcp_data;

    if (d_enable_tracking == true)
        {
            /*
             * Receiver signal alignment
             */
            if (d_pull_in == true)
                {
                    int samples_offset;

                    // 28/11/2011 ACQ to TRK transition BUG CORRECTION
                    float acq_trk_shif_correction_samples;
                    int acq_to_trk_delay_samples;
                    acq_to_trk_delay_samples = d_sample_counter - d_acq_sample_stamp;
                    acq_trk_shif_correction_samples = d_next_prn_length_samples - fmod((float)acq_to_trk_delay_samples, (float)d_next_prn_length_samples);
                    samples_offset = round(d_acq_code_phase_samples + acq_trk_shif_correction_samples);
                    // /todo: Check if the sample counter sent to the next block as a time reference should be incremented AFTER sended or BEFORE
                    d_sample_counter_seconds = d_sample_counter_seconds + (((double)samples_offset) / (double)d_fs_in);
                    d_sample_counter = d_sample_counter + samples_offset; //count for the processed samples
                    d_pull_in = false;
                    consume_each(samples_offset); //shift input to perform alignement with local replica
                    return 1;
                }

            // GNSS_SYNCHRO OBJECT to interchange data between tracking->telemetry_decoder
            Gnss_Synchro current_synchro_data;
            // Fill the acquisition data
            current_synchro_data = *d_acquisition_gnss_synchro;

            const gr_complex* in = (gr_complex*) input_items[0]; //PRN start block alignement
            Gnss_Synchro **out = (Gnss_Synchro **) &output_items[0];

            // Update the prn length based on code freq (variable) and
            // sampling frequency (fixed)
            // variable code PRN sample block size
            d_current_prn_length_samples = d_next_prn_length_samples;

            update_local_code();
            update_local_carrier();

            // perform Early, Prompt and Late correlation
            d_correlator.Carrier_wipeoff_and_EPL_volk(d_current_prn_length_samples,
                    in,
                    d_carr_sign,
                    d_early_code,
                    d_prompt_code,
                    d_late_code,
                    d_Early,
                    d_Prompt,
                    d_Late,
                    is_unaligned());

            // check for samples consistency (this should be done before in the receiver / here only if the source is a file)
            if (std::isnan((*d_Prompt).real()) == true or std::isnan((*d_Prompt).imag()) == true )// or std::isinf(in[i].real())==true or std::isinf(in[i].imag())==true)
                {
                    const int samples_available = ninput_items[0];
                    d_sample_counter = d_sample_counter + samples_available;
                    LOG(WARNING) << "Detected NaN samples at sample number " << d_sample_counter;
                    consume_each(samples_available);

                    // make an output to not stop the rest of the processing blocks
                    current_synchro_data.Prompt_I = 0.0;
                    current_synchro_data.Prompt_Q = 0.0;
                    current_synchro_data.Tracking_timestamp_secs = d_sample_counter_seconds;
                    current_synchro_data.Carrier_phase_rads = 0.0;
                    current_synchro_data.Code_phase_secs = 0.0;
                    current_synchro_data.CN0_dB_hz = 0.0;
                    current_synchro_data.Flag_valid_tracking = false;

                    *out[0] =current_synchro_data;

                    return 1;
                }

            //! Variable used for control
            d_control_id++;

            //! Send and receive a TCP packet
            boost::array<float, NUM_TX_VARIABLES_GPS_L1_CA> tx_variables_array = {{d_control_id,
                                                                                   (*d_Early).real(),
                                                                                   (*d_Early).imag(),
                                                                                   (*d_Late).real(),
                                                                                   (*d_Late).imag(),
                                                                                   (*d_Prompt).real(),
                                                                                   (*d_Prompt).imag(),
                                                                                   d_acq_carrier_doppler_hz,
                                                                                   1}};
            d_tcp_com.send_receive_tcp_packet_gps_l1_ca(tx_variables_array, &tcp_data);

            //! Recover the tracking data
            code_error = tcp_data.proc_pack_code_error;
            carr_error = tcp_data.proc_pack_carr_error;
            // Modify carrier freq based on NCO command
            d_carrier_doppler_hz = tcp_data.proc_pack_carrier_doppler_hz;
            // Modify code freq based on NCO command
            code_nco = 1/(1/GPS_L1_CA_CODE_RATE_HZ - code_error/GPS_L1_CA_CODE_LENGTH_CHIPS);
            d_code_freq_hz = code_nco;

            // Update the phasestep based on code freq (variable) and
            // sampling frequency (fixed)
            d_code_phase_step_chips = d_code_freq_hz / (float)d_fs_in; //[chips]
            // variable code PRN sample block size
            float T_chip_seconds;
            float T_prn_seconds;
            float T_prn_samples;
            float K_blk_samples;
            T_chip_seconds = 1 / d_code_freq_hz;
            T_prn_seconds = T_chip_seconds * GPS_L1_CA_CODE_LENGTH_CHIPS;
            T_prn_samples = T_prn_seconds * d_fs_in;
            d_rem_code_phase_samples = d_next_rem_code_phase_samples;
            K_blk_samples = T_prn_samples + d_rem_code_phase_samples;//-code_error*(float)d_fs_in;

            // Update the current PRN delay (code phase in samples)
            float T_prn_true_seconds = GPS_L1_CA_CODE_LENGTH_CHIPS / GPS_L1_CA_CODE_RATE_HZ;
            float T_prn_true_samples = T_prn_true_seconds * (float)d_fs_in;
            d_code_phase_samples = d_code_phase_samples + T_prn_samples - T_prn_true_samples;
            if (d_code_phase_samples < 0)
                {
                    d_code_phase_samples = T_prn_true_samples + d_code_phase_samples;
                }

            d_code_phase_samples = fmod(d_code_phase_samples, T_prn_true_samples);
            d_next_prn_length_samples = round(K_blk_samples); //round to a discrete samples
            d_next_rem_code_phase_samples = K_blk_samples - d_next_prn_length_samples; //rounding error

            /*!
             * \todo Improve the lock detection algorithm!
             */
            // ####### CN0 ESTIMATION AND LOCK DETECTORS ######
            if (d_cn0_estimation_counter < CN0_ESTIMATION_SAMPLES)
                {
                    // fill buffer with prompt correlator output values
                    d_Prompt_buffer[d_cn0_estimation_counter] = *d_Prompt;
                    d_cn0_estimation_counter++;
                }
            else
                {
                    d_cn0_estimation_counter = 0;
                    d_CN0_SNV_dB_Hz = cn0_svn_estimator(d_Prompt_buffer, CN0_ESTIMATION_SAMPLES, d_fs_in, GPS_L1_CA_CODE_LENGTH_CHIPS);
                    d_carrier_lock_test = carrier_lock_detector(d_Prompt_buffer, CN0_ESTIMATION_SAMPLES);

                    // ###### TRACKING UNLOCK NOTIFICATION #####
                    if (d_carrier_lock_test < d_carrier_lock_threshold or d_CN0_SNV_dB_Hz < MINIMUM_VALID_CN0)
                        {
                            d_carrier_lock_fail_counter++;
                        }
                    else
                        {
                            if (d_carrier_lock_fail_counter > 0) d_carrier_lock_fail_counter--;
                        }
                    if (d_carrier_lock_fail_counter > MAXIMUM_LOCK_FAIL_COUNTER)
                        {
                            std::cout << "Loss of lock in channel " << d_channel << "!" << std::endl;
                            LOG(INFO) << "Loss of lock in channel " << d_channel << "!";
                            ControlMessageFactory* cmf = new ControlMessageFactory();
                            if (d_queue != gr::msg_queue::sptr()) {
                                    d_queue->handle(cmf->GetQueueMessage(d_channel, 2));
                            }
                            delete cmf;
                            d_carrier_lock_fail_counter = 0;
                            d_enable_tracking = false; // TODO: check if disabling tracking is consistent with the channel state machine

                        }
                }

            // ########### Output the tracking data to navigation and PVT ##########

            current_synchro_data.Prompt_I = (double)(*d_Prompt).real();
            current_synchro_data.Prompt_Q = (double)(*d_Prompt).imag();
            current_synchro_data.Tracking_timestamp_secs = d_sample_counter_seconds;
            current_synchro_data.Carrier_phase_rads = (double)d_acc_carrier_phase_rad;
            current_synchro_data.Carrier_Doppler_hz = (double)d_carrier_doppler_hz;
            current_synchro_data.Code_phase_secs = (double)d_code_phase_samples * (1/(float)d_fs_in);
            current_synchro_data.CN0_dB_hz = (double)d_CN0_SNV_dB_Hz;
            *out[0] = current_synchro_data;

            // ########## DEBUG OUTPUT
            /*!
             *  \todo The stop timer has to be moved to the signal source!
             */
            // debug: Second counter in channel 0
            if (d_channel == 0)
                {
                    if (floor(d_sample_counter / d_fs_in) != d_last_seg)
                        {
                            d_last_seg = floor(d_sample_counter / d_fs_in);
                            std::cout << "Current input signal time = " << d_last_seg << " [s]" << std::endl;
                            LOG(INFO) << "Tracking CH " << d_channel <<  ": Satellite " << Gnss_Satellite(systemName[sys], d_acquisition_gnss_synchro->PRN)
                                      << ", CN0 = " << d_CN0_SNV_dB_Hz << " [dB-Hz]";
                        }
                }
            else
                {
                    if (floor(d_sample_counter / d_fs_in) != d_last_seg)
                        {
                            d_last_seg = floor(d_sample_counter / d_fs_in);
                            LOG(INFO) << "Tracking CH " << d_channel <<  ": Satellite " << Gnss_Satellite(systemName[sys], d_acquisition_gnss_synchro->PRN)
                                      << ", CN0 = " << d_CN0_SNV_dB_Hz << " [dB-Hz]";
                        }
                }
        }
    else
        {
			// ########## DEBUG OUTPUT (TIME ONLY for channel 0 when tracking is disabled)
			/*!
			 *  \todo The stop timer has to be moved to the signal source!
			 */
			// stream to collect cout calls to improve thread safety
			std::stringstream tmp_str_stream;
			if (floor(d_sample_counter / d_fs_in) != d_last_seg)
			{
				d_last_seg = floor(d_sample_counter / d_fs_in);

				if (d_channel == 0)
				{
					// debug: Second counter in channel 0
					tmp_str_stream << "Current input signal time = " << d_last_seg << " [s]" << std::endl << std::flush;
					std::cout << tmp_str_stream.rdbuf() << std::flush;
				}
			}
            *d_Early = gr_complex(0,0);
            *d_Prompt = gr_complex(0,0);
            *d_Late = gr_complex(0,0);
            Gnss_Synchro **out = (Gnss_Synchro **) &output_items[0]; //block output streams pointer
            // GNSS_SYNCHRO OBJECT to interchange data between tracking->telemetry_decoder
            *out[0] = *d_acquisition_gnss_synchro;

            //! When tracking is disabled an array of 1's is sent to maintain the TCP connection
            boost::array<float, NUM_TX_VARIABLES_GPS_L1_CA> tx_variables_array = {{1,1,1,1,1,1,1,1,0}};
            d_tcp_com.send_receive_tcp_packet_gps_l1_ca(tx_variables_array, &tcp_data);
        }

    if(d_dump)
        {
            // MULTIPLEXED FILE RECORDING - Record results to file
            float prompt_I;
            float prompt_Q;
            float tmp_E, tmp_P, tmp_L;
            float tmp_float;
            prompt_I = (*d_Prompt).real();
            prompt_Q = (*d_Prompt).imag();
            tmp_E = std::abs<float>(*d_Early);
            tmp_P = std::abs<float>(*d_Prompt);
            tmp_L = std::abs<float>(*d_Late);
            try
            {
                    // EPR
                    d_dump_file.write((char*)&tmp_E, sizeof(float));
                    d_dump_file.write((char*)&tmp_P, sizeof(float));
                    d_dump_file.write((char*)&tmp_L, sizeof(float));
                    // PROMPT I and Q (to analyze navigation symbols)
                    d_dump_file.write((char*)&prompt_I, sizeof(float));
                    d_dump_file.write((char*)&prompt_Q, sizeof(float));
                    // PRN start sample stamp
                    //tmp_float=(float)d_sample_counter;
                    d_dump_file.write((char*)&d_sample_counter, sizeof(unsigned long int));
                    // accumulated carrier phase
                    d_dump_file.write((char*)&d_acc_carrier_phase_rad, sizeof(float));

                    // carrier and code frequency
                    d_dump_file.write((char*)&d_carrier_doppler_hz, sizeof(float));
                    d_dump_file.write((char*)&d_code_freq_hz, sizeof(float));

                    //PLL commands
                    d_dump_file.write((char*)&carr_error, sizeof(float));
                    d_dump_file.write((char*)&carr_nco, sizeof(float));

                    //DLL commands
                    d_dump_file.write((char*)&code_error, sizeof(float));
                    d_dump_file.write((char*)&code_nco, sizeof(float));

                    // CN0 and carrier lock test
                    d_dump_file.write((char*)&d_CN0_SNV_dB_Hz, sizeof(float));
                    d_dump_file.write((char*)&d_carrier_lock_test, sizeof(float));

                    // AUX vars (for debug purposes)
                    tmp_float = 0;
                    d_dump_file.write((char*)&tmp_float, sizeof(float));
                    d_dump_file.write((char*)&d_sample_counter_seconds, sizeof(double));
            }
            catch (std::ifstream::failure e)
            {
                    LOG(WARNING) << "Exception writing trk dump file " << e.what();
            }
        }

    consume_each(d_current_prn_length_samples); // this is necessary in gr::block derivates
    d_sample_counter_seconds = d_sample_counter_seconds + ( ((double)d_current_prn_length_samples) / (double)d_fs_in );
    d_sample_counter += d_current_prn_length_samples; //count for the processed samples
    return 1; //output tracking result ALWAYS even in the case of d_enable_tracking==false
}



void Gps_L1_Ca_Tcp_Connector_Tracking_cc::set_channel(unsigned int channel)
{
    d_channel = channel;
    LOG(INFO) << "Tracking Channel set to " << d_channel;
    // ############# ENABLE DATA FILE LOG #################
    if (d_dump == true)
        {
            if (d_dump_file.is_open() == false)
                {
                    try
                    {
                            d_dump_filename.append(boost::lexical_cast<std::string>(d_channel));
                            d_dump_filename.append(".dat");
                            d_dump_file.exceptions (std::ifstream::failbit | std::ifstream::badbit);
                            d_dump_file.open(d_dump_filename.c_str(), std::ios::out | std::ios::binary);
                            LOG(INFO) << "Tracking dump enabled on channel " << d_channel << " Log file: " << d_dump_filename.c_str();
                    }
                    catch (std::ifstream::failure e)
                    {
                            LOG(WARNING) << "channel " << d_channel << " Exception opening trk dump file " << e.what();
                    }
                }
        }

    //! Listen for connections on a TCP port
    if (d_listen_connection == true)
        {
            d_port = d_port_ch0 + d_channel;
            d_listen_connection = d_tcp_com.listen_tcp_connection(d_port, d_port_ch0);
        }
}



void Gps_L1_Ca_Tcp_Connector_Tracking_cc::set_channel_queue(concurrent_queue<int> *channel_internal_queue)
{
    d_channel_internal_queue = channel_internal_queue;
}

void Gps_L1_Ca_Tcp_Connector_Tracking_cc::set_gnss_synchro(Gnss_Synchro* p_gnss_synchro)
{
    d_acquisition_gnss_synchro = p_gnss_synchro;
    //	Gnss_Satellite(satellite.get_system(), satellite.get_PRN());
    //DLOG(INFO) << "Tracking code phase set to " << d_acq_code_phase_samples;
    //DLOG(INFO) << "Tracking carrier doppler set to " << d_acq_carrier_doppler_hz;
    //DLOG(INFO) << "Tracking Satellite set to " << d_satellite;

}
