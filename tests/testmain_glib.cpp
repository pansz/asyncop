#include <glib.h>

int test_main_asyncop();

int main(int argc, char *argv[])
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    // Start tests after a short delay to let the loop start
    g_timeout_add(50, [](gpointer data) -> gboolean {
        test_main_asyncop();
        g_main_loop_quit((GMainLoop*)data);
        return G_SOURCE_REMOVE;
    }, loop);

    g_main_loop_run(loop);
    g_main_loop_unref(loop);

    return 0;
}