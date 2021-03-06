/*
 * Generated by gdbus-codegen 2.56.4. DO NOT EDIT.
 *
 * The license of this code is the same as for the D-Bus interface description
 * it was derived from.
 */

#ifndef __BARESIPBUS_H__
#define __BARESIPBUS_H__

#include <gio/gio.h>

G_BEGIN_DECLS


/* ------------------------------------------------------------------------ */
/* Declarations for com.creytiv.Baresip */

#define DBUS_TYPE_BARESIP (dbus_baresip_get_type ())
#define DBUS_BARESIP(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), DBUS_TYPE_BARESIP, DBusBaresip))
#define DBUS_IS_BARESIP(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), DBUS_TYPE_BARESIP))
#define DBUS_BARESIP_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), DBUS_TYPE_BARESIP, DBusBaresipIface))

struct _DBusBaresip;
typedef struct _DBusBaresip DBusBaresip;
typedef struct _DBusBaresipIface DBusBaresipIface;

struct _DBusBaresipIface
{
  GTypeInterface parent_iface;


  gboolean (*handle_invoke) (
    DBusBaresip *object,
    GDBusMethodInvocation *invocation,
    const gchar *arg_command);

  void (*event) (
    DBusBaresip *object,
    const gchar *arg_class,
    const gchar *arg_evtype,
    const gchar *arg_param);

};

GType dbus_baresip_get_type (void) G_GNUC_CONST;

GDBusInterfaceInfo *dbus_baresip_interface_info (void);
guint dbus_baresip_override_properties (GObjectClass *klass, guint property_id_begin);


/* D-Bus method call completion functions: */
void dbus_baresip_complete_invoke (
    DBusBaresip *object,
    GDBusMethodInvocation *invocation,
    const gchar *response);



/* D-Bus signal emissions functions: */
void dbus_baresip_emit_event (
    DBusBaresip *object,
    const gchar *arg_class,
    const gchar *arg_evtype,
    const gchar *arg_param);



/* D-Bus method calls: */
void dbus_baresip_call_invoke (
    DBusBaresip *proxy,
    const gchar *arg_command,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean dbus_baresip_call_invoke_finish (
    DBusBaresip *proxy,
    gchar **out_response,
    GAsyncResult *res,
    GError **error);

gboolean dbus_baresip_call_invoke_sync (
    DBusBaresip *proxy,
    const gchar *arg_command,
    gchar **out_response,
    GCancellable *cancellable,
    GError **error);



/* ---- */

#define DBUS_TYPE_BARESIP_PROXY (dbus_baresip_proxy_get_type ())
#define DBUS_BARESIP_PROXY(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), DBUS_TYPE_BARESIP_PROXY, DBusBaresipProxy))
#define DBUS_BARESIP_PROXY_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), DBUS_TYPE_BARESIP_PROXY, DBusBaresipProxyClass))
#define DBUS_BARESIP_PROXY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), DBUS_TYPE_BARESIP_PROXY, DBusBaresipProxyClass))
#define DBUS_IS_BARESIP_PROXY(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), DBUS_TYPE_BARESIP_PROXY))
#define DBUS_IS_BARESIP_PROXY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), DBUS_TYPE_BARESIP_PROXY))

typedef struct _DBusBaresipProxy DBusBaresipProxy;
typedef struct _DBusBaresipProxyClass DBusBaresipProxyClass;
typedef struct _DBusBaresipProxyPrivate DBusBaresipProxyPrivate;

struct _DBusBaresipProxy
{
  /*< private >*/
  GDBusProxy parent_instance;
  DBusBaresipProxyPrivate *priv;
};

struct _DBusBaresipProxyClass
{
  GDBusProxyClass parent_class;
};

GType dbus_baresip_proxy_get_type (void) G_GNUC_CONST;

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (DBusBaresipProxy, g_object_unref)
#endif

void dbus_baresip_proxy_new (
    GDBusConnection     *connection,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GAsyncReadyCallback  callback,
    gpointer             user_data);
DBusBaresip *dbus_baresip_proxy_new_finish (
    GAsyncResult        *res,
    GError             **error);
DBusBaresip *dbus_baresip_proxy_new_sync (
    GDBusConnection     *connection,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GError             **error);

void dbus_baresip_proxy_new_for_bus (
    GBusType             bus_type,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GAsyncReadyCallback  callback,
    gpointer             user_data);
DBusBaresip *dbus_baresip_proxy_new_for_bus_finish (
    GAsyncResult        *res,
    GError             **error);
DBusBaresip *dbus_baresip_proxy_new_for_bus_sync (
    GBusType             bus_type,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GError             **error);


/* ---- */

#define DBUS_TYPE_BARESIP_SKELETON (dbus_baresip_skeleton_get_type ())
#define DBUS_BARESIP_SKELETON(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), DBUS_TYPE_BARESIP_SKELETON, DBusBaresipSkeleton))
#define DBUS_BARESIP_SKELETON_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), DBUS_TYPE_BARESIP_SKELETON, DBusBaresipSkeletonClass))
#define DBUS_BARESIP_SKELETON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), DBUS_TYPE_BARESIP_SKELETON, DBusBaresipSkeletonClass))
#define DBUS_IS_BARESIP_SKELETON(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), DBUS_TYPE_BARESIP_SKELETON))
#define DBUS_IS_BARESIP_SKELETON_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), DBUS_TYPE_BARESIP_SKELETON))

typedef struct _DBusBaresipSkeleton DBusBaresipSkeleton;
typedef struct _DBusBaresipSkeletonClass DBusBaresipSkeletonClass;
typedef struct _DBusBaresipSkeletonPrivate DBusBaresipSkeletonPrivate;

struct _DBusBaresipSkeleton
{
  /*< private >*/
  GDBusInterfaceSkeleton parent_instance;
  DBusBaresipSkeletonPrivate *priv;
};

struct _DBusBaresipSkeletonClass
{
  GDBusInterfaceSkeletonClass parent_class;
};

GType dbus_baresip_skeleton_get_type (void) G_GNUC_CONST;

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (DBusBaresipSkeleton, g_object_unref)
#endif

DBusBaresip *dbus_baresip_skeleton_new (void);


G_END_DECLS

#endif /* __BARESIPBUS_H__ */
