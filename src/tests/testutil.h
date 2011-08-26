/* this really should be in GLib */
#include <gio/gio.h>

#define _g_assert_property_notify(object, property_name)                \
  do                                                                    \
    {                                                                   \
      if (!G_IS_OBJECT (object))                                        \
        {                                                               \
          g_assertion_message (G_LOG_DOMAIN,                            \
                               __FILE__,                                \
                               __LINE__,                                \
                               G_STRFUNC,                               \
                               "Not a GObject instance");               \
        }                                                               \
      if (g_object_class_find_property (G_OBJECT_GET_CLASS (object),    \
                                        property_name) == NULL)         \
        {                                                               \
          g_assertion_message (G_LOG_DOMAIN,                            \
                               __FILE__,                                \
                               __LINE__,                                \
                               G_STRFUNC,                               \
                               "Property " property_name " does not "   \
                               "exist on object");                      \
        }                                                               \
      if (_g_assert_property_notify_run (object, property_name))        \
        {                                                               \
          g_assertion_message (G_LOG_DOMAIN,                            \
                               __FILE__,                                \
                               __LINE__,                                \
                               G_STRFUNC,                               \
                               "Timed out waiting for notification "    \
                               "on property " property_name);           \
        }                                                               \
    }                                                                   \
  while (FALSE)

#define _g_assert_signal_received(object, signal_name, callback, user_data) \
  do                                                                    \
    {                                                                   \
      if (!G_IS_OBJECT (object))                                        \
        {                                                               \
          g_assertion_message (G_LOG_DOMAIN,                            \
                               __FILE__,                                \
                               __LINE__,                                \
                               G_STRFUNC,                               \
                               "Not a GObject instance");               \
        }                                                               \
      if (g_signal_lookup (signal_name,                                 \
                           G_TYPE_FROM_INSTANCE (object)) == 0)         \
        {                                                               \
          g_assertion_message (G_LOG_DOMAIN,                            \
                               __FILE__,                                \
                               __LINE__,                                \
                               G_STRFUNC,                               \
                               "Signal `" signal_name "' does not "     \
                               "exist on object");                      \
        }                                                               \
      if (_g_assert_signal_received_run (object, signal_name, callback, user_data)) \
        {                                                               \
          g_assertion_message (G_LOG_DOMAIN,                            \
                               __FILE__,                                \
                               __LINE__,                                \
                               G_STRFUNC,                               \
                               "Timed out waiting for signal `"         \
                               signal_name "'");                        \
        }                                                               \
    }                                                                   \
  while (FALSE)

gboolean _g_assert_property_notify_run (gpointer     object,
                                        const gchar *property_name);


gboolean _g_assert_signal_received_run (gpointer     object,
                                        const gchar *signal_name,
                                        GCallback    callback,
                                        gpointer     user_data);
