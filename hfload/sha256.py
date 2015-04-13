
# Reference implementation of SHA-256.

# Note: The bitwise complement of x is x ^ 0xffffffff.  (Assuming x 32 bits long.)

# Fix: Organize the different formats and make sure we have conversion
#      functions to list containing bytes, which should be the canonical
#      internal format.  Some formats to consider:
#      hex
#      Python binary string
#      Midstate format: list of eight 32-bit words
# 
#      The external format should be Python binary string, like hashlib.

def Ch(x, y, z):
    assert x >= 0 and x < (1 << 32)
    assert y >= 0 and y < (1 << 32)
    assert z >= 0 and z < (1 << 32)
    return (x & y) ^ ((x ^ 0xffffffff) & z)

def Maj(x, y, z):
    assert x >= 0 and x < (1 << 32)
    assert y >= 0 and y < (1 << 32)
    assert z >= 0 and z < (1 << 32)
    return (x & y) ^ (x & z) ^ (y & z)

def ROTR(n, x):
    assert n >= 0 and n < 32
    assert x >=0 and x < (1 << 32)
    return (x >> n) | ((x << (32-n)) & 0xffffffff)

def SHR(n, x):
    assert n >= 0 and n < 32
    assert x >=0 and x < (1 << 32)
    return x >> n;

def BigSigmaZero(x):
    assert x >=0 and x < (1 << 32)
    return ROTR(2, x) ^ ROTR(13, x) ^ ROTR(22, x)

def BigSigmaOne(x):
    assert x >=0 and x < (1 << 32)
    return ROTR(6, x) ^ ROTR(11, x) ^ ROTR(25, x)

def LittleSigmaZero(x):
    assert x >=0 and x < (1 << 32)
    return ROTR(7, x) ^ ROTR(18, x) ^ SHR(3, x)

def LittleSigmaOne(x):
    assert x >=0 and x < (1 << 32)
    return ROTR(17, x) ^ ROTR(19, x) ^ SHR(10, x)

# Fix: Confirm correctness of these constants.
K = [0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
     0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
     0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
     0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
     0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
     0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
     0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
     0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2]

def int_to_lebytes(integer, digit):
    assert digit > 0
    assert integer >= 0 and integer < 256 ** digit
    result = []
    for i in range(digit):
        result = result + [integer % 256]
        integer = integer >> 8
    return result

def int_to_bebytes(integer, digit):
    lebytes = int_to_lebytes(integer, digit)
    lebytes.reverse()
    return lebytes

def bebytes_to_int(bebytes):
    assert {x >= 0 and x < 256 for x in bebytes} == set([True])
    result = 0
    for x in bebytes:
        result = 256*result + x
    return result

# Fix: Write some test code?  A check function for correct padding
#      could be used by parse_message() in an assert.
def pad_message(M, bytecount=None):
    l = 8 * len(M)
    a = (l + 1) % 512
    if a > 448:
        k = 512 + 448 - a
    else:
        k = 448 - a
    assert (k+1) % 8 == 0
    zero_blocks = int((k+1)/8) - 1
    if bytecount is None:
        padded = M + [0x80] + [0x00] * zero_blocks + int_to_bebytes(l, 8)
    else:
        padded = M + [0x80] + [0x00] * zero_blocks + int_to_bebytes(8 * bytecount, 8)        
    return padded

def parse_message(M_padded):
    assert len(M_padded) % 64 == 0
    N = int(len(M_padded) / 64)
    result = [None] * N
    for i in range(N):
        ith_byte_set = M_padded[64*i:64*(i+1)]
        ith_word_set = [None] * 16
        for j in range(16):
            ith_word_set[j] = bebytes_to_int(ith_byte_set[4*j:4*(j+1)])
        result[i] = ith_word_set
    return result

H_const = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]

def sha256_internal(parsed_message, midstate=None):
    if midstate is None:
        H0, H1, H2, H3, H4, H5, H6, H7 = H_const
    else:
        assert len(midstate) == 8
        H0, H1, H2, H3, H4, H5, H6, H7 = midstate
    # Fix: Also check the range values in each 16 byte set.
    assert {len(x) == 16 for x in parsed_message} == set([True])
    N = len(parsed_message)
    for i in range(N):
        M = parsed_message[i]
        W = [None] * 64
        for t in range(16):
            W[t] = M[t]
        for t in range(16,64):
            W[t] = (LittleSigmaOne(W[t-2]) + W[t-7] + LittleSigmaZero(W[t-15]) + W[t-16]) & 0xffffffff
        a = H0
        b = H1
        c = H2
        d = H3
        e = H4
        f = H5
        g = H6
        h = H7
        for t in range(64):
            T1 = (h + BigSigmaOne(e) + Ch(e,f,g) + K[t] + W[t]) & 0xffffffff
            T2 = (BigSigmaZero(a) + Maj(a,b,c)) & 0xffffffff
            h = g
            g = f
            f = e
            e = (d + T1) & 0xffffffff
            d = c
            c = b
            b = a
            a = (T1 + T2) & 0xffffffff
        H0 = (a + H0) & 0xffffffff
        H1 = (b + H1) & 0xffffffff
        H2 = (c + H2) & 0xffffffff
        H3 = (d + H3) & 0xffffffff
        H4 = (e + H4) & 0xffffffff
        H5 = (f + H5) & 0xffffffff
        H6 = (g + H6) & 0xffffffff
        H7 = (h + H7) & 0xffffffff
    message_digest_word_list = [H0, H1, H2, H3, H4, H5, H6, H7]
    return message_digest_word_list

def hash_words_to_bstring(hws):
    assert len(hws) == 8
    assert {x >=0 and x < (1 << 32) for x in hws} == set([True])
    hw_quartets = [int_to_bebytes(x, 4) for x in hws]
    message_digest_list = []
    for q in hw_quartets:
        message_digest_list = message_digest_list + q
    message_digest = bytes(message_digest_list)
    return message_digest

def sha256(M, midstate=None, bytecount=None):
    M_list = list(M)
    padded = pad_message(M_list, bytecount=bytecount)
    parsed = parse_message(padded)
    hash_words = sha256_internal(parsed, midstate=midstate)
    message_digest = hash_words_to_bstring(hash_words)
    return message_digest

# Fix: Work this into sha256().
def sha256_with_midstate(M, midstate, bytecount):
    M_list = list(M)
    padded = pad_message(M_list, bytecount=bytecount)
    parsed = parse_message(padded)
    message_digest_list = sha256_internal(parsed, midstate=midstate)
    return message_digest_list

def sha256_midstate(M):
    assert len(M) % 64 == 0
    parsed = parse_message(M)
    midstate = sha256_internal(parsed)
    return midstate

# Fix: Just for testing.  Move to a test program?
def test_midstate(M):
    assert len(M) > 64
    # First, the standard value.
    a = pad_message(M)
    b = parse_message(a)
    c = sha256_internal(b)
    print("No midstate: %s" % (c))
    # Second, we try it using a midstate.
    d = M[0:64]
    e = sha256_midstate(d)
    f = pad_message(M[64:], len(M))
    g = parse_message(f)
    h = sha256_internal(g, e)
    print("Using midstate: %s" % (h))
    # Third, we try it using our convenience function.
    i = M[0:64]
    j = sha256_midstate(i)
    k = sha256_with_midstate(M[64:], j, len(M))
    print("Using sha256_with_midstate: %s" % (k))

# Fix: Standardize on "hash words" or "midstate".
def bytes_to_midstate(bytelist):
    assert len(bytelist) == 32
    assert {x >=0 and x < 256 for x in bytelist} == set([True])
    result = []
    for i in range(8):
        result = result + [bebytes_to_int(bytelist[4*i:4*(i+1)])]
    return result

def midstate_to_bytes(midstate):
    assert len(midstate) == 8
    assert {x >=0 and x < (1 << 32) for x in midstate} == set([True])
    hw_quartets = [int_to_bebytes(x, 4) for x in midstate]
    bytelist = []
    for q in hw_quartets:
        bytelist = bytelist + q
    return bytelist

# This behaves exactly like cgminer's calc_midstate() function.
def cgminer_calc_midstate(sixty_four_bytes):
    assert len(sixty_four_bytes) == 64
    assert {x >=0 and x < 256 for x in sixty_four_bytes} == set([True])
    sfb = sixty_four_bytes.copy()
    sfb = reverse_every_four_bytes(sfb)
    midstate = sha256_midstate(sfb)
    midstate_bytes = midstate_to_bytes(midstate)
    midstate_bytes = reverse_every_four_bytes(midstate_bytes)
    return midstate_bytes

def reverse_every_four_bytes(bytelist):
    assert len(bytelist) % 4 == 0
    assert {x >= 0 and x < 256 for x in bytelist} == set([True])
    result = []
    for i in range(int(len(bytelist)/4)):
        a = bytelist[4*i:4*i+4]
        a.reverse()
        result = result + a
    return result

# Fix: This should be oriented to operating on lists of bytes.
def cgminer_regen_hash(eighty_bytes):
    assert len(eighty_bytes) == 80
    assert {x >=0 and x < 256 for x in eighty_bytes} == set([True])
    eb = eighty_bytes.copy()
    eb = reverse_every_four_bytes(eb)
    hash1 = sha256(bytes(eb))
    hash2 = sha256(hash1)
    return hash2
