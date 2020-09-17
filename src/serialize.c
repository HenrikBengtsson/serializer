


#include <R.h>
#include <Rinternals.h>
#include <Rdefines.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// The data buffer.
// Needs total length and pos to keep track of how much data it contains
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
typedef struct {
  R_xlen_t length;
  R_xlen_t pos;
  unsigned char *data;
} buffer;



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Initialise an empty buffer to hold 'nbytes'
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
buffer *init_buffer(int nbytes) {
  buffer *buf = (buffer *)malloc(sizeof(buffer));
  if (buf == NULL) {
    error("init_buffer(): cannot malloc buffer");
  }

  buf->data = (unsigned char *)malloc(nbytes * sizeof(unsigned char));
  if (buf->data == NULL) {
    error("init_buffer(): cannot malloc buffer data");
  }

  buf->length = nbytes;
  buf->pos = 0;

  return buf;
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Naive buffer expansion - double it every time space runs out
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void expand_buffer(buffer *buf) {

  double factor = 2;
  buf->length = (R_xlen_t)(factor * buf->length);

  if (buf->length > R_XLEN_T_MAX) {
    error("Requested buffer expandsion too large: %td\n", buf->length);
  }

  unsigned char *new_data = (unsigned char *)realloc((void *)buf->data, buf->length);

  if (new_data == NULL) {
    free(buf->data);
    free(buf);
    error("Couldn't expand buffer to reallocate: %td\n", buf->length);
  }

  buf->data = new_data;

}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Write a byte into the buffer at the current location.
// The actual buffer is encapsulated as part of the stream structure, so you
// have to extract it first
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void write_byte(R_outpstream_t stream, int c) {
  buffer *buf = (buffer *)stream->data;

  // Expand the buffer if it's out space
  while (buf->pos >= buf->length) {
    expand_buffer(buf);
  }

  buf->data[buf->pos++] = (unsigned char)c;
}



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Write multiple bytes into the buffer at the current location.
// The actual buffer is encapsulated as part of the stream structure, so you
// have to extract it first
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void write_bytes(R_outpstream_t stream, void *src, int length) {
  buffer *buf = (buffer *)stream->data;

  // Expand the buffer if it's out space
  while (buf->pos + length > buf->length) {
    expand_buffer(buf);
  }

  memcpy(buf->data + buf->pos, src, length);

  buf->pos += length;
}



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Read a byte from the serialized stream
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
int read_byte(R_inpstream_t stream) {
  buffer *buf = (buffer *)stream->data;

  if (buf->pos >= buf->length) {
    error("read_byte(): overflow");
  }

  return buf->data[buf->pos++];
}



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Read multiple bytes from the serialized stream
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void read_bytes(R_inpstream_t stream, void *dst, int length) {
  buffer *buf = (buffer *)stream->data;

  if (buf->pos + length > buf->length) {
    error("read_bytes(): overflow");
  }

  memcpy(dst, buf->data + buf->pos, length);

  buf->pos += length;
}



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Serialize an R object
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
SEXP pack_(SEXP robj) {

  // Create the buffer for the serialized representation
  // See also: `expand_buffer()` which re-allocates the memory buffer if
  // it runs out of space
  buffer *buf = init_buffer(16384);

  // Create the output stream structure
  struct R_outpstream_st output_stream;

  // Initialise the output stream structure
  R_InitOutPStream(
    &output_stream,          // The stream object which wraps everything
    (R_pstream_data_t) buf,  // The actual data
    R_pstream_binary_format, // Store as binary
    3,                       // Version = 3 for R >3.5.0 See `?base::serialize`
    write_byte,              // Function to write single byte to buffer
    write_bytes,             // Function for writing multiple bytes to buffer
    NULL,                    // Func for special handling of reference data.
    R_NilValue               // Data related to reference data handling
  );

  // Serialize the object into the output_stream
  R_Serialize(robj, &output_stream);

  // Copy just the valid bytes to return to the user
  SEXP res_ = PROTECT(allocVector(RAWSXP, buf->pos));
  memcpy(RAW(res_), buf->data, buf->pos);

  // Free all the memory
  free(buf->data);
  free(buf);
  UNPROTECT(1);
  return res_;
}



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Unpack a raw vector to an R object
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
SEXP unpack_(SEXP vec_) {

  if (TYPEOF(vec_) != RAWSXP) {
    error("unpack(): Only raw vectors can be unserialized");
  }

  // Unpack the raw vector into a C void *
  void *vec = RAW(vec_);
  R_xlen_t len = XLENGTH(vec_);

  // Create a buffer object which points to the raw data
  buffer *buf = malloc(sizeof(buffer));
  if (buf == NULL) {
    error("'buf' malloc failed!");
  }
  buf->length = len;
  buf->pos    = 0;
  buf->data   = vec;

  // Treat the data buffer as an input stream
  struct R_inpstream_st input_stream;

  R_InitInPStream(
    &input_stream,           // Stream object wrapping data buffer
    (R_pstream_data_t) buf,  // Actual data buffer
    R_pstream_any_format,    // Unpack all serialized types
    read_byte,               // Function to read single byte from buffer
    read_bytes,              // Function for reading multiple bytes from buffer
    NULL,                    // Func for special handling of reference data.
    NULL                     // Data related to reference data handling
  );

  // Unserialize the input_stream into an R object
  SEXP res_  = PROTECT(R_Unserialize(&input_stream));

  free(buf);
  UNPROTECT(1);
  return res_;
}
