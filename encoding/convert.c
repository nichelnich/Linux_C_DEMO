int FW2UTF8Convert( const wchar_t* a_szSrc, int a_nSrcSize, char* a_szDest, int a_nDestSize )
{
#ifdef WINDOWS
  return WideCharToMultiByte( CP_UTF8, 0, a_szSrc, -1, a_szDest, a_nDestSize, NULL, NULL );
#else
  size_t result;
  iconv_t env;
  env = iconv_open("UTF-8","WCHAR_T");
  if (env==(iconv_t)-1)
  {
    printf("iconv_open WCHAR_T->UTF8 error%s %d\n",strerror(errno),errno) ;
    return -1;
  }
  memset(a_szDest, 0, a_nDestSize );
  result = iconv(env,(char**)&a_szSrc,(size_t*)&a_nSrcSize,(char**)&a_szDest,(size_t*)&a_nDestSize);
  if (result==(size_t)-1)
  {
    printf("iconv WCHAR_T->UTF8 error %d\n",errno) ;
    return -1;
  }
  iconv_close(env);
  return (int)result;
#endif
}


PJ_DEF(wchar_t*) ansi_to_unicode(const char *utf8, pj_size_t length, wchar_t* outbuf, pj_size_t buf_count)
{
  const char* pc = (const char*)utf8;
  const char* last = pc + length;

  unsigned int b;
  unsigned int num_errors = 0;
  int i = 0;
  int wlen = buf_count/sizeof(wchar_t);

  memset(outbuf, 0, buf_count);

  if(!utf8 || length == 0)
    return 1;

  while (pc < last && i < wlen)
  {
    b = *pc++;

    if( !b ) break; // 0 - is eos in all utf encodings

    if ((b & 0x80) == 0)
    {
      // 1-byte sequence: 000000000xxxxxxx = 0xxxxxxx
      ;
    } 
    else if ((b & 0xe0) == 0xc0) 
    {
      // 2-byte sequence: 00000yyyyyxxxxxx = 110yyyyy 10xxxxxx
      if(pc == last) { outbuf[i++]='?'; ++num_errors; break; }
      b = (b & 0x1f) << 6;
      b |= (*pc++ & 0x3f);
    } 
    else if ((b & 0xf0) == 0xe0) 
    {
      // 3-byte sequence: zzzzyyyyyyxxxxxx = 1110zzzz 10yyyyyy 10xxxxxx
      if(pc >= last - 1) { outbuf[i++]='?'; ++num_errors; break; }

      b = (b & 0x0f) << 12;
      b |= (*pc++ & 0x3f) << 6;
      b |= (*pc++ & 0x3f);
      if(b == 0xFEFF &&
        i == 0) // bom at start
        continue; // skip it
    } 
    else if ((b & 0xf8) == 0xf0) 
    {
      // 4-byte sequence: 11101110wwwwzzzzyy + 110111yyyyxxxxxx = 11110uuu 10uuzzzz 10yyyyyy 10xxxxxx
      if(pc >= last - 2) { outbuf[i++]='?'; break; }

      b = (b & 0x07) << 18;
      b |= (*pc++ & 0x3f) << 12;
      b |= (*pc++ & 0x3f) << 6;
      b |= (*pc++ & 0x3f);
      // b shall contain now full 21-bit unicode code point.
      assert((b & 0x1fffff) == b);
      if((b & 0x1fffff) != b)
      {
        outbuf[i++]='?';
        ++num_errors;
        continue;
      }
      if( sizeof(wchar_t) == 16 ) // Seems like Windows, wchar_t is utf16 code units sequence there.
      {
        outbuf[i++] = (wchar_t)(0xd7c0 + (b >> 10));
        outbuf[i++] = (wchar_t)(0xdc00 | (b & 0x3ff));
      }
      else if( sizeof(wchar_t) >= 21 ) // wchar_t is full ucs-4 
      {
        outbuf[i++] = (wchar_t)(b);
      }
      else
      {
        assert(0); // what? wchar_t is single byte here?
      }
    } 
    else 
    {
      return NULL;
      assert(0); //bad start for UTF-8 multi-byte sequence"
      ++num_errors;
      b = '?';
    }
    outbuf[i++] = (wchar_t)(b);
  }

  return outbuf;
}

char* unicode_to_ansi( const wchar_t *wstr, size_t len,
                 char *buf, size_t buf_size)
{
  int i = 0;
  int wlen;
  int  num_errors = 0;
  unsigned int c;
  const wchar_t *pc = wstr;
  const wchar_t *end;
  //PJ_ASSERT_RETURN(wstr && buf, NULL);

  wlen = wcslen(wstr);
  end = (wlen > len/sizeof(wchar_t)) ? (pc + len/sizeof(wchar_t)) : (pc + wlen);
  memset(buf, 0, buf_size);

  for (c = *pc; pc < end; c = *(++pc)) {
    if (c < (1 << 7)) {
      if (i >= buf_size) {
        break;
      }
      buf[i++] = (char)(c);
    } else if (c < (1 << 11)) {
      if (i >= buf_size-1) {
        break;
      }
      buf[i++] = (char)((c >> 6) | 0xc0);
      buf[i++] = (char)((c & 0x3f) | 0x80);
    } else if (c < (1 << 16)) {
      if (i >= buf_size-2) {
        break;
      }
      buf[i++] = (char)((c >> 12) | 0xe0);
      buf[i++] = (char)(((c >> 6) & 0x3f) | 0x80);
      buf[i++] = (char)((c & 0x3f) | 0x80);
    } else if (c < (1 << 21)) {
      if (i >= buf_size-3) {
        break;
      }
      buf[i++] = (char)((c >> 18) | 0xf0);
      buf[i++] = (char)(((c >> 12) & 0x3f) | 0x80);
      buf[i++] = (char)(((c >> 6) & 0x3f) | 0x80);
      buf[i++] = (char)((c & 0x3f) | 0x80);
    } else {
      num_errors = 1;
      break;
    }
  }
  if(num_errors) {
    return NULL;
  }else {
    return buf;
  }
}