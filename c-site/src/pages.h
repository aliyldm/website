/* pages.h - Yonlendirici ve sayfa uretimi. */
#ifndef PAGES_H
#define PAGES_H

#include "http.h"

/* Istegi uygun sayfaya yonlendirir ve yaniti doldurur.
 * store_lock() altinda cagrilmalidir. */
void route_request(const Request *req, Response *res);

/* Hata sayfasi uretir (404, 405, 413 ...). detail NULL olabilir. */
void render_error(const Request *req, Response *res, int status, const char *detail);

#endif
