// Copyright 2003 Tom Felker
//
// This file is part of VLevel.
//
// VLevel is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// VLevel is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with VLevel; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

// vlevel-bin.cpp - the vlevel-bin command, uses VolumeLeveler

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

#include <stdio.h>

#include <limits.h>
#include <assert.h>

#include "vlevel.h"
#include "volumeleveler.h"
#include "commandline.h"

using namespace std;

void ToValues(char *in, value_t *out, size_t values, unsigned int bits_per_value);
void FromValues(value_t *in, char *out, size_t values, unsigned int bits_per_value);
void LevelRaw(istream &in, ostream& out, VolumeLeveler &vl, unsigned int bits_per_value);
void Help();

// len is num of values, in needs len*bits/8 chars
void ToValues(char *in, value_t *out, size_t values, unsigned int bits_per_value)
{
  switch(bits_per_value) {
  case 16:
    for(size_t i = 0; i < values; ++i) out[i] = (value_t)((short *)in)[i] / (SHRT_MAX + 1);
    break;
  default:
    assert(false);
  }
}

// len is num of samples, out needs len*bits/8 chars
void FromValues(value_t *in, char *out, size_t values, unsigned int bits_per_value)
{
  switch(bits_per_value) {
  case 16:
    for(size_t i = 0; i < values; ++i) ((short *)out)[i] = (short)(in[i] * SHRT_MAX);
    break;
  default:
    assert(false);
  }
}

void LevelRaw(FILE *in, FILE *out, VolumeLeveler &vl, unsigned int bits_per_value)
{
  assert(bits_per_value % 8 == 0);
  
  // figure out the size of things
  size_t samples = vl.GetSamples();
  size_t channels = vl.GetChannels();
  size_t values = samples * channels;
  size_t bytes_per_value = bits_per_value / 8;
  size_t bytes = values * bytes_per_value;

  // allocate our interleaved buffers
  char *raw_buf = new char[bytes];
  value_t *raw_value_buf = new value_t[values];

  // allocate our per-channel buffers
  value_t **bufs = new value_t*[channels];
  for(size_t ch = 0; ch < channels; ++ch)
    bufs[ch] = new value_t[samples];
  
  // how much data in the buffer is good
  size_t good_values, good_samples;
  // how much from the front of the buffer should be ignored
  size_t silence_values, silence_samples;

  while(!ferror(in) && !feof(in)) {
    // read and convert to value_t
    good_values = fread(raw_buf, bytes_per_value, values, in);
    good_samples = good_values / channels;
    ToValues(raw_buf, raw_value_buf, good_values, bits_per_value);

    // de-interleave the data
    for(size_t s = 0; s < good_samples; ++s)
      for(size_t ch = 0; ch < channels; ++ch)
	bufs[ch][s] = raw_value_buf[s * channels + ch];
    
    // do the exchange
    silence_samples = vl.Exchange(bufs, bufs, good_samples);
    silence_values = silence_samples * channels;
    good_samples -= silence_samples;
    good_values -= silence_values;

    // interleave the data
    for(size_t s = silence_samples; s < silence_samples + good_samples; ++s)
      for(size_t ch = 0; ch < channels; ++ch)
	raw_value_buf[s * channels + ch] = bufs[ch][s];
    
    // write the data
    FromValues(&raw_value_buf[silence_values], raw_buf, good_values, bits_per_value);
    fwrite(raw_buf, bytes_per_value, good_values, out);
  }

  // silence the data
  for(size_t s = 0; s < samples; ++s)
    for(size_t ch = 0; ch < channels; ++ch)
      bufs[ch][s] = 0;
  
  // exchange the data, 
  silence_samples = vl.Exchange(bufs, bufs, samples);
  silence_values = silence_samples * channels;
  // good_samples = samples - silence_samples;
  good_values = values - silence_values;

  //interlace
  for(size_t s = silence_samples; s < samples; ++s)
    for(size_t ch = 0; ch < channels; ++ch)
      raw_value_buf[s * channels + ch] = bufs[ch][s];
    
  FromValues(&raw_value_buf[silence_values], raw_buf, good_values, bits_per_value);
  fwrite(raw_buf, bytes_per_value, good_values, out);

  delete [] raw_buf;
  delete [] raw_value_buf;
  for(size_t ch = 0; ch < channels; ++ch)
    delete [] bufs[ch];
  delete [] bufs;
  
/*
  while(in) {
    in.read(raw_buf, bytes);
    size_t good_values = in.gcount() / bytes_per_value;
    size_t good_samples = good_values / channels;
    ToValues(raw_buf, value_buf, good_values, bits_per_value);
    size_t silence_samples = vl.Exchange(value_buf, value_buf, good_samples);
    size_t silence_values = silence_samples * channels;    
    good_values -= silence_values;
    FromValues(&value_buf[silence_values], raw_buf, good_values, bits_per_value);
    out.write(raw_buf, good_values * bytes_per_value);
  }
  for(size_t i = 0; i < values; ++i) value_buf[i] = 0;
  size_t silence_samples = vl.Exchange(value_buf, value_buf, samples);
  size_t silence_values = silence_samples * channels;    
  FromValues(&value_buf[silence_values], raw_buf, values - silence_values, bits_per_value);
  out.write(raw_buf, (values - silence_values) * bytes_per_value);
  
  delete [] raw_buf;
  delete [] value_buf;
*/  
}

void Help()
{
  cerr << "VLevel v0.5" << endl
       << endl
       << "usage:" << endl
       << "\tvlevel-bin [options] < infile > outfile" << endl
       << endl
       << "options: (abbreviations also work)" << endl
       << "\t--length num" << endl
       << "\t\tSets the buffer to num samples long" << endl
       << "\t\tDefault is 132300 (three seconds at 44.1kHz)" << endl
       << "\t--channels num" << endl
       << "\t\tEach sample has num channels" << endl
       << "\t\tDefault is 2" << endl
       << "\t--strength num" << endl
       << "\t\tEffect strength, 1 is max, 0 is no effect." << endl
       << "\t\tDefault is .8" << endl
       << "\t--max-multiplier num" << endl
       << "\t\tSets the maximum amount a sample will be multiplied" << endl
       << "\t\tDefault is 20" << endl
       << "\t--undo" << endl
       << "\t\tReverses the effect of a previous VLevel" << endl;
}

int main(int argc, char *argv[])
{
  CommandLine cmd(argc, argv);
  size_t length = 3 * 44100;
  size_t channels = 2;
  value_t strength = .8, max_multiplier = 20;
  bool undo = false;
  string option, argument;
  
  while(option = cmd.GetOption(), !option.empty()) {
    
    if(option == "length" || option == "l") {
      if((istringstream(cmd.GetArgument()) >> length).fail()) {
        cerr << cmd.GetProgramName() << ": bad or no option for --length" << endl;
        return 2;
      }
      if(length < 2) {
	cerr << cmd.GetProgramName() << ": --length must be greater than 1" << endl;
        return 2;
      }
    } else if(option == "channels" || option == "c") {
      if((istringstream(cmd.GetArgument()) >> channels).fail()) {
        cerr << cmd.GetProgramName() << ": bad or no option for --channels" << endl;
        return 2;
      }
      if(channels < 1) {
	cerr << cmd.GetProgramName() << ": --channels must be greater than 0" << endl;
        return 2;
      }
    } else if(option == "strength" || option == "s") {
      if((istringstream(cmd.GetArgument()) >> strength).fail()) {
        cerr << cmd.GetProgramName() << ": bad or no option for --strength" << endl;
        return 2;
      }
      if(strength < 0 || strength > 1) {
	cerr << cmd.GetProgramName() << ": --strength must be between 0 and 1 inclusive." << endl;
        return 2;
      }
    } else if(option == "max-multiplier" || option == "m") {
      if((istringstream(cmd.GetArgument()) >> max_multiplier).fail()) {
        cerr << cmd.GetProgramName() << ": bad or no option for --max-multiplier" << endl
	     << cmd.GetProgramName() << ": for no max multiplier, give a negative number" << endl;
        return 2;
      }
    } else if(option == "undo" || option == "u") {
      undo = true;
    } else if(option == "help" || option == "h") {
      Help();
      return 0;
    } else {
      cerr << cmd.GetProgramName() << ": unrecognized option " << option << endl;
      Help();
      return 2;
    }
  }
  
  // This works, see docs/technical.txt
  if(undo) strength = strength / (strength - 1);
  
  cerr << "Beginning VLevel with:" << endl
       << "length: " << length << endl
       << "channels: " << channels << endl
       << "strength: " << strength << endl
       << "max_multiplier: " << max_multiplier << endl;
  
  VolumeLeveler l(length, channels, strength, max_multiplier);
  LevelRaw(stdin, stdout, l, 16);
  return 0;
}
