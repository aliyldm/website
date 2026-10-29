/* blog.h - Programin icine gomulu blog yazilari. */
#ifndef BLOG_H
#define BLOG_H

typedef struct {
    const char *slug;     /* adreste kullanilir: /blog/<slug> */
    const char *title;
    const char *summary;
    const char *tags;     /* virgulle ayrilmis: "http,ag" */
    int         y, m, d;
    const char *body;     /* guvenilir, elle yazilmis HTML */
} Post;

extern const Post POSTS[];  /* yeniden eskiye sirali */
extern const int  POST_COUNT;

#endif
