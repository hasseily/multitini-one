
#include <ctype.h>
#include <algorithm>
#include <stdexcept>
#include "GSBString.h"

String::String(const char *arg1, const char *arg2)
{
	int len1 = strlen(arg1);
	int len2 = strlen(arg2);
	Build(arg1, len1, arg2, len2);
}

String::String(char init, int count)
{
    if(count<0||count>UINT16_MAX)throw std::length_error("BASIC string exceeds 65535 bytes");
	// RefCntPtr_t already has a one-byte array in it, so this allocation has room
	// for a nul terminator
	_p = (struct RefCntPtr *) new uint8_t[sizeof(RefCntPtr) + count];
	memset(_p->text, (char) init, count);
	_p->text[count] = '\0';
	_p->refCnt = 1;
	_p->length = (uint16_t) count;
}

void String::Build(const char *arg1, int len1, const char *arg2, int len2)
{
    if(len1<0||len2<0||len1>UINT16_MAX||len2>UINT16_MAX-len1)
        throw std::length_error("BASIC string exceeds 65535 bytes");
	uint16_t totalLen = uint16_t(len1 + len2);
    bool binary=(len1&&memchr(arg1,0,len1))||(len2&&memchr(arg2,0,len2));
	if (totalLen > sizeof(_shortString) - 2 || binary)
	{
		// RefCntPtr_t already has a one-byte array in it, so this allocation has room
		// for a nul terminator
		_p = (struct RefCntPtr *) new uint8_t[sizeof(RefCntPtr) + totalLen];
		memcpy(_p->text, arg1, len1);
		if (len2)
			memcpy(_p->text + len1, arg2, len2);
		_p->text[totalLen] = '\0';
		_p->refCnt = 1;
		_p->length = totalLen;
	}

	else
	{
		_p = (RefCntPtr *) 1;
		if (len1)
			memcpy(_shortString + 1, arg1, len1);
		if (len2)
			memcpy(_shortString + len1 + 1, arg2, len2);
	}
}

String &String::operator=(const char *s)
{
	Release();
	if (s)
	{
		int len = strlen(s);
		Build(s, len, null, 0);
	}
	return *this;
}

String String::itoa(int i)
{
	char buf[20], *p = &buf[sizeof(buf) - 1];
	bool negative = i < 0;
	int64_t ll = i;

	*p = '\0';
	if (negative)
		ll = -ll;
	do
	{
		*--p = ll % 10 + '0';
		ll /= 10;
	} while (ll);
	if (negative)
		*--p = '-';
	return p;
}

String String::ftoa(float f)
{
    // A finite IEEE binary32 needs at most 39 integer digits, a sign,
    // decimal point and six fractional digits with this format.
    char s[64];
    snprintf(s, sizeof s, "%f", (double) f);
    // truncate
    int i = 0;
    for (i=strlen(s); i>=0; i--)
    {
      if (s[i] == 0) continue;
      if (s[i] == '0') {s[i] = 0;continue;}
      if (s[i] == '.') {s[i] = 0;break;}
      break;
    }
    if (i==0) s[0] = '0';
    return s;
}

String String::Substr(int start, int count) const
{
	String result;
    if(start<0||count<0)throw std::out_of_range("Negative BASIC substring range");
	count = std::min(count, int(length()) - start);
	if (start < (int) length() && count > 0)
	{
		result.Build(c_str() + start, count, null, 0);
	}
	return result;
}
