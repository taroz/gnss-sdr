/*!
 * \file gps_l1_ca_telemetry_decoder_cc.cc
 * \brief Navigation message demodulator based on the Kay Borre book MATLAB-based GPS receiver
 * \author Javier Arribas, 2011. jarribas(at)cttc.es
 *
 * -------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2011  (see AUTHORS file for a list of contributors)
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

/*!
 * \todo Clean this code and move the telemetri definitions to GPS_L1_CA system definitions file
 */
#include "gps_l1_ca_telemetry_decoder_cc.h"

#include "control_message_factory.h"

#include <iostream>
#include <sstream>
#include <bitset>

#include <gnuradio/gr_io_signature.h>

#include <glog/log_severity.h>
#include <glog/logging.h>


using google::LogMessage;
/*!
 * \todo name and move the magic numbers to GPS_L1_CA.h
 */
gps_l1_ca_telemetry_decoder_cc_sptr
gps_l1_ca_make_telemetry_decoder_cc(unsigned int satellite, long if_freq, long fs_in, unsigned
    int vector_length, gr_msg_queue_sptr queue, bool dump) {

  return gps_l1_ca_telemetry_decoder_cc_sptr(new gps_l1_ca_telemetry_decoder_cc(satellite, if_freq,
      fs_in, vector_length, queue, dump));
}

void gps_l1_ca_telemetry_decoder_cc::forecast (int noutput_items,
    gr_vector_int &ninput_items_required){
  for (unsigned i = 0; i < 3; i++) {
    ninput_items_required[i] =d_samples_per_bit*8; //set the required sample history
  }
}


gps_l1_ca_telemetry_decoder_cc::gps_l1_ca_telemetry_decoder_cc(unsigned int satellite, long if_freq, long fs_in, unsigned
    int vector_length, gr_msg_queue_sptr queue, bool dump) :
    gr_block ("gps_navigation_cc", gr_make_io_signature (5, 5, sizeof(double)),
        gr_make_io_signature(1, 1, sizeof(gnss_synchro))) {
  // initialize internal vars
  d_queue = queue;
  d_dump = dump;
  d_satellite = satellite;
  d_vector_length = vector_length;
  d_samples_per_bit=20; // it is exactly 1000*(1/50)=20
  d_fs_in=fs_in;
  d_preamble_duration_seconds=(1.0/(float)GPS_CA_TELEMETRY_RATE_BITS_SECOND)*(float)GPS_CA_PREAMBLE_LENGTH_BITS;
  //std::cout<<"d_preamble_duration_seconds="<<d_preamble_duration_seconds<<"\r\n";
  // set the preamble
  unsigned short int preambles_bits[8]=GPS_PREAMBLE;

  memcpy((unsigned short int*)this->d_preambles_bits, (unsigned short int*)preambles_bits, 8* sizeof(unsigned short int));

  // preamble bits to sampled symbols
  d_preambles_symbols=(signed int*)malloc(sizeof(signed int)*8*d_samples_per_bit);
  int n=0;
  for (int i=0;i<8;i++)
    {
    for (unsigned int j=0;j<d_samples_per_bit;j++)
      {
      if (d_preambles_bits[i]==1)
        {
        d_preambles_symbols[n]=1;
        }else{
          d_preambles_symbols[n]=-1;
        }
      n++;
      }
    }
  d_sample_counter=0;
  d_preamble_code_phase_seconds=0;
  d_stat=0;
  d_preamble_index=0;
  d_symbol_accumulator_counter=0;
  d_frame_bit_index=0;

  d_flag_frame_sync=false;
  d_GPS_frame_4bytes=0;
  d_prev_GPS_frame_4bytes=0;
  d_flag_parity=false;

  //set_history(d_samples_per_bit*8); // At least a history of 8 bits are needed to correlate with the preamble

}

gps_l1_ca_telemetry_decoder_cc::~gps_l1_ca_telemetry_decoder_cc() {
  delete d_preambles_symbols;
  d_dump_file.close();

}

int gps_l1_ca_telemetry_decoder_cc::general_work (int noutput_items, gr_vector_int &ninput_items,
    gr_vector_const_void_star &input_items,	gr_vector_void_star &output_items) {
  int corr_value=0;
  int preamble_diff;

  gnss_synchro gps_synchro; //structure to save the synchronization information
  gnss_synchro **out = (gnss_synchro **) &output_items[0];
  d_sample_counter++; //count for the processed samples

  const double **in = (const double **)  &input_items[0]; //Get the input samples pointer
	// ########### Output the tracking data to navigation and PVT ##########
	// Output channel 0: Prompt correlator output Q
//	*out[0]=(double)d_Prompt.real();
//	// Output channel 1: Prompt correlator output I
//	*out[1]=(double)d_Prompt.imag();
//	// Output channel 2: PRN absolute delay [s]
//	*out[2]=d_sample_counter_seconds;
//	// Output channel 3: d_acc_carrier_phase_rad [rad]
//	*out[3]=(double)d_acc_carrier_phase_rad;
//	// Output channel 4: PRN code phase [s]
//	*out[4]=(double)d_code_phase_samples*(1/(float)d_fs_in);

  /*!
   * \todo Check the HOW GPS time computation, taking into account that the preamble correlation last 160 symbols, which is 160 ms in GPS CA L1
   */
  // FIFO history to get the exact timestamp of the first symbol of the preamble
//  if (d_prn_start_sample_history.size()<160)
//  {
//	  // fill the queue
//	  d_prn_start_sample_history.push_front(in[2][0]);
//	  consume_each(1); //one by one
//	  return 1;
//  }else{
//	  d_prn_start_sample_history.pop_back();
//	  d_prn_start_sample_history.push_front(in[2][0]);
//  }
  // TODO Optimize me!
  //******* preamble correlation ********
  for (unsigned int i=0;i<d_samples_per_bit*8;i++){
    if (in[1][i] < 0)	// symbols clipping
      {
      corr_value-=d_preambles_symbols[i];
      }else{
        corr_value+=d_preambles_symbols[i];
      }
  }
  d_flag_preamble=false;
  //******* frame sync ******************
  if (abs(corr_value)>=160){
	  //TODO: Rewrite with state machine
	  if (d_stat==0)
	  {
		  d_GPS_FSM.Event_gps_word_preamble();
		  d_preamble_index=d_sample_counter;//record the preamble sample stamp
		  std::cout<<"Preamble detection for SAT "<<d_satellite<<std::endl;
		  d_symbol_accumulator=0; //sync the symbol to bits integrator
		  d_symbol_accumulator_counter=0;
		  d_frame_bit_index=8;
		  d_stat=1; // enter into frame pre-detection status
	  }else if (d_stat==1) //check 6 seconds of preample separation
	  {
		  preamble_diff=abs(d_sample_counter-d_preamble_index);
		  if (abs(preamble_diff-6000)<1)
		  {
			  d_GPS_FSM.Event_gps_word_preamble();
			  d_flag_preamble=true;
			  d_preamble_index=d_sample_counter;//record the preamble sample stamp (t_P)
			  d_preamble_time_seconds=in[2][0]-d_preamble_duration_seconds; //record the PRN start sample index associated to the preamble
			  d_preamble_code_phase_seconds=in[4][0];

			  if (!d_flag_frame_sync){
				  d_flag_frame_sync=true;
				  std::cout<<" Frame sync SAT "<<d_satellite<<" with preamble start at "<<d_preamble_time_seconds<<" [s]"<<std::endl;
			  }
		  }
	  }
  }else{
	  if (d_stat==1)
	  {
		  preamble_diff=d_sample_counter-d_preamble_index;
		  if (preamble_diff>6001){
			  std::cout<<"Lost of frame sync SAT "<<this->d_satellite<<" preamble_diff= "<<preamble_diff<<std::endl;
			  d_stat=0; //lost of frame sync
			  d_flag_frame_sync=false;
		  }
	  }
  }

  //******* code error accumulator *****
  //d_preamble_phase-=in[3][0];
  //******* SYMBOL TO BIT *******

  d_symbol_accumulator+=in[1][d_samples_per_bit*8-1]; // accumulate the input value in d_symbol_accumulator
  d_symbol_accumulator_counter++;
  if (d_symbol_accumulator_counter==20)
    {
    if (d_symbol_accumulator>0){ //symbol to bit
      d_GPS_frame_4bytes+=1; //insert the telemetry bit in LSB
    }
    d_symbol_accumulator=0;
    d_symbol_accumulator_counter=0;

    //******* bits to words ******
    d_frame_bit_index++;
    if (d_frame_bit_index==30)
      {
      d_frame_bit_index=0;
      //parity check
      //Each word in wordbuff is composed of:
      //      Bits 0 to 29 = the GPS data word
      //      Bits 30 to 31 = 2 LSBs of the GPS word ahead.
      // prepare the extended frame [-2 -1 0 ... 30]
      if (d_prev_GPS_frame_4bytes & 0x00000001)
        {
        d_GPS_frame_4bytes=d_GPS_frame_4bytes|0x40000000;
        }
      if (d_prev_GPS_frame_4bytes & 0x00000002)
        {
        d_GPS_frame_4bytes=d_GPS_frame_4bytes|0x80000000;
        }
      /* Check that the 2 most recently logged words pass parity. Have to first
        invert the data bits according to bit 30 of the previous word. */
      if(d_GPS_frame_4bytes & 0x40000000)
        {
        d_GPS_frame_4bytes ^= 0x3FFFFFC0; // invert the data bits (using XOR)
        }
      if (gps_word_parityCheck(d_GPS_frame_4bytes)) {
        memcpy(&d_GPS_FSM.d_GPS_frame_4bytes,&d_GPS_frame_4bytes,sizeof(char)*4);
        d_GPS_FSM.d_preamble_time_ms=d_preamble_time_seconds*1000.0;
        d_GPS_FSM.Event_gps_word_valid();
        d_flag_parity=true;
      }else{
        d_GPS_FSM.Event_gps_word_invalid();
        d_flag_parity=false;
      }
      d_prev_GPS_frame_4bytes=d_GPS_frame_4bytes; // save the actual frame
      d_GPS_frame_4bytes=d_GPS_frame_4bytes & 0;
      }else{
        d_GPS_frame_4bytes<<=1; //shift 1 bit left the telemetry word
      }
    }

  // output the frame
	consume_each(1); //one by one

	gps_synchro.valid_word=(d_flag_frame_sync==true and d_flag_parity==true);
	gps_synchro.flag_preamble=d_flag_preamble;
	gps_synchro.preamble_delay_ms=d_preamble_time_seconds*1000.0;
	gps_synchro.prn_delay_ms=(in[2][0]-d_preamble_duration_seconds)*1000.0;
	gps_synchro.preamble_code_phase_ms=d_preamble_code_phase_seconds*1000.0;
	gps_synchro.preamble_code_phase_correction_ms=(in[4][0]-d_preamble_code_phase_seconds)*1000.0;
	gps_synchro.satellite_PRN=d_satellite;
	gps_synchro.channel_ID=d_channel;
	*out[0]=gps_synchro;
	return 1;
}


void gps_l1_ca_telemetry_decoder_cc::set_satellite(int satellite) {
  d_satellite = satellite;
  d_GPS_FSM.d_satellite_PRN=satellite;
  LOG_AT_LEVEL(INFO) << "Navigation Satellite set to " << d_satellite;
}

void gps_l1_ca_telemetry_decoder_cc::set_channel(int channel) {
  d_channel = channel;
  d_GPS_FSM.d_channel_ID=channel;
  LOG_AT_LEVEL(INFO) << "Navigation channel set to " << channel;
}

