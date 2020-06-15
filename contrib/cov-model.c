/* GLib types. */
typedef size_t        gsize;
typedef char          gchar;
typedef unsigned char guchar;
typedef int           gint;
typedef unsigned long gulong;
typedef unsigned int  guint32;
typedef void        * gpointer;
typedef unsigned int  gboolean;


void
g_assertion_message_expr (const char *domain,
                          const char *file,
                          int         line,
                          const char *func,
                          const char *expr)
{
  __coverity_panic__ ();
}

#define g_critical(...) __coverity_panic__ ();

/* Treat it as a memory sink to hide one-time allocation leaks. */
void
  (g_once_init_leave) (volatile void *location,
                       gsize result)
{
  __coverity_escape__ (result);
}
