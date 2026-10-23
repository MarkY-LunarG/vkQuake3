#ifndef GAPI_H_
#define GAPI_H_

/*
 * The interface between the Quake 3 renderer and a graphics API.
 *
 * The renderer code that all graphics APIs share uses only the types and
 * functions in this file. Each graphics API in graphics_api/<API>/ defines
 * the structs behind the handles and implements the functions.
 */

// A texture, its sampler state, and the API objects that bind it to a draw.
typedef struct gapi_texture_s gapi_texture_t;

// A graphics pipeline for one set of Q3 shader stage state.
typedef struct gapi_pipeline_s gapi_pipeline_t;

#endif
