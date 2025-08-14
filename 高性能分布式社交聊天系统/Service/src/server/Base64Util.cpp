#include "Base64Util.hpp"
#include <iostream>

using namespace std;

const string Base64Util::base64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

bool Base64Util::is_base64_char(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

string Base64Util::encode(const string& data) {
    return encode(data.c_str(), data.length());
}

string Base64Util::encode(const vector<char>& data) {
    return encode(data.data(), data.size());
}

string Base64Util::encode(const char* data, size_t len) {
    string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';
    }

    return ret;
}

string Base64Util::decode(const string& encoded_data) {
    vector<char> decoded = decode_to_vector(encoded_data);
    return string(decoded.begin(), decoded.end());
}

vector<char> Base64Util::decode_to_vector(const string& encoded_data) {
    int in_len = encoded_data.size();
    int i = 0;
    int j = 0;
    int in = 0;
    unsigned char char_array_4[4], char_array_3[3];
    vector<char> ret;

    while (in_len-- && (encoded_data[in] != '=') && is_base64_char(encoded_data[in])) {
        char_array_4[i++] = encoded_data[in]; in++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 4; j++)
            char_array_4[j] = 0;

        for (j = 0; j < 4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++) ret.push_back(char_array_3[j]);
    }

    return ret;
}

bool Base64Util::is_valid_base64(const string& encoded_data) {
    for (char c : encoded_data) {
        if (!is_base64_char(c) && c != '=') {
            return false;
        }
    }
    
    // 检查填充字符
    size_t padding = 0;
    for (auto it = encoded_data.rbegin(); it != encoded_data.rend() && *it == '='; ++it) {
        padding++;
    }
    
    return padding <= 2 && encoded_data.length() % 4 == 0;
}
