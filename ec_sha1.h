/*!
\file ec_sha1.h

\author Paul E. Jones <paulej@packetizer.com>
\editor jiangyong
\email  kipway@outlook.com

eclib SHA1 encode tooltips

calss cSha1;

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway/eclib

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

简介：
为了方便使用，kipway 适配的sha1编码库，使用ec::encode_sha1即可。
*/

#pragma once

#define SWAPDWORD(a) ( (a << 24) | ((a & 0xFF00) << 8) | ((a & 0xFF0000) >> 8) | (a >> 24))
namespace sha1
{
	class cSha1
	{
	public:
		/*!
		\breif Cal SHA1
		\param pout [out] Not less than 20 bytes
		\remark CAL function add by kipway@outlook.com
		*/
		bool CAL(const void* pd, unsigned size, void* pout)
		{
			Reset();
			Input((unsigned char *)pd, size);
			if (Result((unsigned*)pout)) {
				unsigned int a = 0x12345678;
				if (*((unsigned char*)&a) != 0x12) {
					unsigned int* pdw = (unsigned int*)pout;
					pdw[0] = SWAPDWORD(pdw[0]);
					pdw[1] = SWAPDWORD(pdw[1]);
					pdw[2] = SWAPDWORD(pdw[2]);
					pdw[3] = SWAPDWORD(pdw[3]);
					pdw[4] = SWAPDWORD(pdw[4]);
				}
				return true;
			}
			return false;
		}

		/*!
		\breif  Re-initialize the class
		*/
		void Reset()
		{
			Length_Low = 0;
			Length_High = 0;
			Message_Block_Index = 0;

			H[0] = 0x67452301;
			H[1] = 0xEFCDAB89;
			H[2] = 0x98BADCFE;
			H[3] = 0x10325476;
			H[4] = 0xC3D2E1F0;

			Computed = false;
			Corrupted = false;
		};

		/*!
		\brief  Returns the message digest
		*/
		bool Result(unsigned *message_digest_array)
		{
			int i;

			if (Corrupted) {
				return false;
			}

			if (!Computed) {
				PadMessage();
				Computed = true;
			}

			for (i = 0; i < 5; i++) {
				message_digest_array[i] = H[i];
			}

			return true;
		};

		/*
		*  Provide input to SHA1
		*/
		void Input(const unsigned char *message_array,
			unsigned            length)
		{
			if (!length) {
				return;
			}

			if (Computed || Corrupted) {
				Corrupted = true;
				return;
			}

			while (length-- && !Corrupted) {
				Message_Block[Message_Block_Index++] = (*message_array & 0xFF);

				Length_Low += 8;
				Length_Low &= 0xFFFFFFFF;// Force it to 32 bits
				if (Length_Low == 0) {
					Length_High++;
					Length_High &= 0xFFFFFFFF;// Force it to 32 bits
					if (Length_High == 0) {
						Corrupted = true;// Message is too long
					}
				}

				if (Message_Block_Index == 64) {
					ProcessMessageBlock();
				}

				message_array++;
			}
		};
		void Input(const char  *message_array,
			unsigned    length)
		{
			Input((unsigned char *)message_array, length);
		};
		void Input(unsigned char message_element)
		{
			Input(&message_element, 1);
		};
		void Input(char message_element)
		{
			Input((unsigned char *)&message_element, 1);
		};
		cSha1& operator<<(const char *message_array)
		{
			const char *p = message_array;

			while (*p) {
				Input(*p);
				p++;
			}

			return *this;
		};
		cSha1& operator<<(const unsigned char *message_array)
		{
			const unsigned char *p = message_array;

			while (*p) {
				Input(*p);
				p++;
			}

			return *this;
		};
		cSha1& operator<<(const char message_element)
		{
			Input((unsigned char *)&message_element, 1);

			return *this;
		};
		cSha1& operator<<(const unsigned char message_element)
		{
			Input(&message_element, 1);

			return *this;
		};

	private:

		/*
		*  ProcessMessageBlock
		*
		*  Description:
		*      This function will process the next 512 bits of the message
		*      stored in the Message_Block array.
		*
		*  Parameters:
		*      None.
		*
		*  Returns:
		*      Nothing.
		*
		*  Comments:
		*      Many of the variable names in this function, especially the single
		*      character names, were used because those were the names used
		*      in the publication.
		*
		*/

		/*!
		\brief  Process the next 512 bits of the message
		*/
		void ProcessMessageBlock()
		{
			const unsigned K[] = {// Constants defined for SHA-1
				0x5A827999,
				0x6ED9EBA1,
				0x8F1BBCDC,
				0xCA62C1D6
			};
			int         t;// Loop counter
			unsigned    temp; // Temporary word value
			unsigned    W[80]; // Word sequence
			unsigned    A, B, C, D, E;// Word buffers

			/*
			*  Initialize the first 16 words in the array W
			*/
			for (t = 0; t < 16; t++) {
				W[t] = ((unsigned)Message_Block[t * 4]) << 24;
				W[t] |= ((unsigned)Message_Block[t * 4 + 1]) << 16;
				W[t] |= ((unsigned)Message_Block[t * 4 + 2]) << 8;
				W[t] |= ((unsigned)Message_Block[t * 4 + 3]);
			}

			for (t = 16; t < 80; t++) {
				W[t] = CircularShift(1, W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16]);
			}

			A = H[0];
			B = H[1];
			C = H[2];
			D = H[3];
			E = H[4];

			for (t = 0; t < 20; t++) {
				temp = CircularShift(5, A) + ((B & C) | ((~B) & D)) + E + W[t] + K[0];
				temp &= 0xFFFFFFFF;
				E = D;
				D = C;
				C = CircularShift(30, B);
				B = A;
				A = temp;
			}

			for (t = 20; t < 40; t++) {
				temp = CircularShift(5, A) + (B ^ C ^ D) + E + W[t] + K[1];
				temp &= 0xFFFFFFFF;
				E = D;
				D = C;
				C = CircularShift(30, B);
				B = A;
				A = temp;
			}

			for (t = 40; t < 60; t++) {
				temp = CircularShift(5, A) +
					((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
				temp &= 0xFFFFFFFF;
				E = D;
				D = C;
				C = CircularShift(30, B);
				B = A;
				A = temp;
			}

			for (t = 60; t < 80; t++) {
				temp = CircularShift(5, A) + (B ^ C ^ D) + E + W[t] + K[3];
				temp &= 0xFFFFFFFF;
				E = D;
				D = C;
				C = CircularShift(30, B);
				B = A;
				A = temp;
			}

			H[0] = (H[0] + A) & 0xFFFFFFFF;
			H[1] = (H[1] + B) & 0xFFFFFFFF;
			H[2] = (H[2] + C) & 0xFFFFFFFF;
			H[3] = (H[3] + D) & 0xFFFFFFFF;
			H[4] = (H[4] + E) & 0xFFFFFFFF;

			Message_Block_Index = 0;
		};

		/*
		*  PadMessage
		*
		*  Description:
		*      According to the standard, the message must be padded to an even
		*      512 bits.  The first padding bit must be a '1'.  The last 64 bits
		*      represent the length of the original message.  All bits in between
		*      should be 0.  This function will pad the message according to those
		*      rules by filling the message_block array accordingly.  It will also
		*      call ProcessMessageBlock() appropriately.  When it returns, it
		*      can be assumed that the message digest has been computed.
		*
		*  Parameters:
		*      None.
		*
		*  Returns:
		*      Nothing.
		*
		*  Comments:
		*
		*/

		/*!
		\brief  Pads the current message block to 512 bits
		*/
		void PadMessage()
		{
			/*
			*  Check to see if the current message block is too small to hold
			*  the initial padding bits and length.  If so, we will pad the
			*  block, process it, and then continue padding into a second block.
			*/
			if (Message_Block_Index > 55) {
				Message_Block[Message_Block_Index++] = 0x80;
				while (Message_Block_Index < 64) {
					Message_Block[Message_Block_Index++] = 0;
				}

				ProcessMessageBlock();

				while (Message_Block_Index < 56) {
					Message_Block[Message_Block_Index++] = 0;
				}
			}
			else {
				Message_Block[Message_Block_Index++] = 0x80;
				while (Message_Block_Index < 56) {
					Message_Block[Message_Block_Index++] = 0;
				}
			}

			/*
			*  Store the message length as the last 8 octets
			*/
			Message_Block[56] = (Length_High >> 24) & 0xFF;
			Message_Block[57] = (Length_High >> 16) & 0xFF;
			Message_Block[58] = (Length_High >> 8) & 0xFF;
			Message_Block[59] = (Length_High) & 0xFF;
			Message_Block[60] = (Length_Low >> 24) & 0xFF;
			Message_Block[61] = (Length_Low >> 16) & 0xFF;
			Message_Block[62] = (Length_Low >> 8) & 0xFF;
			Message_Block[63] = (Length_Low) & 0xFF;

			ProcessMessageBlock();
		}
		;

		/*
		*  Performs a circular left shift operation
		*/
		inline unsigned CircularShift(int bits, unsigned word)
		{
			return ((word << bits) & 0xFFFFFFFF) | ((word & 0xFFFFFFFF) >> (32 - bits));
		};

		unsigned H[5]; // Message digest buffers

		unsigned Length_Low; // Message length in bits
		unsigned Length_High;  // Message length in bits

		unsigned char Message_Block[64]; // 512-bit message blocks
		int Message_Block_Index; // Index into message block array

		bool Computed; // Is the digest computed?
		bool Corrupted; // Is the message digest corruped?
	};
}

namespace ec
{
	inline bool encode_sha1(const void* pd, unsigned int size, void* pout)
	{
		sha1::cSha1 sh;
		return sh.CAL(pd, size, pout);
	}
}
