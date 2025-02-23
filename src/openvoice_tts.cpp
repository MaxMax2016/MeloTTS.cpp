/**
 * Copyright      2024    Tong Qiu (tong.qiu@intel.com)
 *
 * See LICENSE for clarification regarding multiple authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <cassert>
#include <fstream>
#include <iterator>
#include <array>
#include "openvoice_tts.h"
#include "info_data.h"

namespace melo {
    /* The function 'tts_infer' serves as the entry point for TTS inference.
       1. The parameters 'phones', 'tones', and 'lang_ids' are not declared with 'const' because they are involved in the construction of ov::Tensor objects. 
       2.  Additionally, the numeric parameters 'speaker_id', 'spd_ratio', 'noise_scale', and 'noise_scale_w' are explicitly copied to ensure the correct data type and byte length are passed to the ov::Tensor constructor. 
       This explicit copying is to match the expected data types for the ov::Tensor construction.*/
    std::vector<float> OpenVoiceTTS::tts_infer(std::vector<int64_t>& phones_, std::vector<int64_t>& tones_, std::vector<int64_t>& lang_ids_, 
        const std::vector<std::vector<float>>& phone_level_feature, const float& speed_,
        const int& speaker_id_, bool disable_bert, const float& sdp_ratio_, const float& noise_scale_, const float& noise_scale_w_) {
        size_t n = phones_.size();
        //calculate ja_bert bert
        size_t row = n, col = 768;
        assert(row==tones_.size()&&row==lang_ids_.size() && "phones_.size()==tones_.size()==phone_level_feature.size()");
        
        std::vector<float>ja_bert_data, bert_data(1024*row, 0.0f);
        if(!disable_bert){
            assert(phone_level_feature.front().size() == col && "phone_level_feature.front().size()==768");
            assert(phone_level_feature.size() == row && "phone_level_feature.size() should be equal to phones.size");
            ja_bert_data.reserve(row*col);
#ifdef MELO_DEBUG
            std::cout << "[" << row << "," << col << "]" << std::endl;
#endif
            for (int k = 0; k < col; ++k)
            {
                for (int j = 0; j < row; ++j)
                {
                    ja_bert_data.emplace_back(phone_level_feature[j][k]);
                }
            }
        }
        else
            ja_bert_data.resize(row * col,0.0f);
        //tts infer
        /*  0 phones
            1 phones_length
            2 speakers
            3 tones
            4 lang_ids
            5 bert
            6 ja_bert
            7 noise_scale
            8 length_scale
            9 noise_scale_w
            10 sdp_ratio*/
        // set input tensor

        ov::Tensor phones(ov::element::i64, { BATCH_SIZE, n },phones_.data());
        int64_t len = static_cast<int64_t>(n);
        ov::Tensor phones_length(ov::element::i64, {BATCH_SIZE}, &len);
        _speakers = static_cast<int64_t>(speaker_id_);
        ov::Tensor speakers(ov::element::i64, {BATCH_SIZE}, &_speakers);
        ov::Tensor tones(ov::element::i64,{BATCH_SIZE, n},tones_.data());
        ov::Tensor lang_ids(ov::element::i64, { BATCH_SIZE, n }, lang_ids_.data());
        ov::Tensor bert(ov::element::f32, {BATCH_SIZE, 1024, row}, bert_data.data());
        ov::Tensor ja_bert(ov::element::f32, { BATCH_SIZE, 768, row }, ja_bert_data.data());
        _noise_scale = noise_scale_;
        ov::Tensor noise_scale(ov::element::f32, { BATCH_SIZE }, &_noise_scale);
        _length_scale = 1/speed_;
        ov::Tensor length_scale(ov::element::f32, { BATCH_SIZE }, &_length_scale);
        _noise_scale_w = noise_scale_w_;
        ov::Tensor noise_scale_w(ov::element::f32, { BATCH_SIZE }, &_noise_scale_w);
        _sdp_ration = sdp_ratio_;
        ov::Tensor sdp_ratio(ov::element::f32, { BATCH_SIZE }, &_sdp_ration);
        //std::cout << "tts set_input_tensor\n";
        assert((_infer_request.get()!=nullptr) && "openvoice_tts::_infer_request should not be null!");
        _infer_request->set_input_tensor(0, phones);
        _infer_request->set_input_tensor(1, phones_length);
        _infer_request->set_input_tensor(2, speakers);
        _infer_request->set_input_tensor(3, tones);
        _infer_request->set_input_tensor(4, lang_ids);
        _infer_request->set_input_tensor(5, bert);
        _infer_request->set_input_tensor(6, ja_bert);
        _infer_request->set_input_tensor(7, noise_scale);
        _infer_request->set_input_tensor(8, length_scale);
        _infer_request->set_input_tensor(9, noise_scale_w);
        _infer_request->set_input_tensor(10, sdp_ratio);
        
        ov_infer();

        return get_ouput();
    }
    void OpenVoiceTTS::ov_infer() {
        //std::cout << "tts infer begin\n";
            _infer_request->infer();
        //std::cout << "tts inferok\n";
    }
    std::vector<float> OpenVoiceTTS::get_ouput() {
        const float* output = _infer_request->get_output_tensor(0).data<float>();
        size_t output_size = _infer_request->get_output_tensor(0).get_byte_size()/sizeof(float);
#ifdef MELO_DEBUG
        std::cout <<"OpenVoiceTTS::get_ouput output_size"<<  output_size << std::endl;
#endif
        std::vector<float> wavs(output, output+output_size);
        /*memcpy(wavs.data(), output, output_size * sizeof(float));*/
        return wavs;
    }
    void OpenVoiceTTS::write_wave(const std::string& filename, int32_t sampling_rate, const float* samples, int32_t n)
    {

        melo::WaveHeader header;
        header.chunk_id = 0x46464952;     // FFIR
        header.format = 0x45564157;       // EVAW
        header.subchunk1_id = 0x20746d66; // "fmt "
        header.subchunk1_size = 16;       // 16 for PCM
        header.audio_format = 1;          // PCM =1

        int32_t num_channels = 1;
        int32_t bits_per_sample = 16; // int16_t
        header.num_channels = num_channels;
        header.sample_rate = sampling_rate;
        header.byte_rate = sampling_rate * num_channels * bits_per_sample / 8;
        header.block_align = num_channels * bits_per_sample / 8;
        header.bits_per_sample = bits_per_sample;
        header.subchunk2_id = 0x61746164; // atad
        header.subchunk2_size = n * num_channels * bits_per_sample / 8;

        header.chunk_size = 36 + header.subchunk2_size;

        std::vector<int16_t> samples_int16(n);
        for (int32_t i = 0; i != n; ++i)
        {
            samples_int16[i] = samples[i] * 32676;
        }

        std::ofstream os(filename, std::ios::binary);
        if (!os)
        {
            std::string msg = "Failed to create " + filename;
        }

        os.write(reinterpret_cast<const char*>(&header), sizeof(header));
        os.write(reinterpret_cast<const char*>(samples_int16.data()),
            samples_int16.size() * sizeof(int16_t));

        std::cout << "write wav to "<< filename << std::endl;
        return;
    }

}
