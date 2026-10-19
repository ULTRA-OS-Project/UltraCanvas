/* Tests/AtspiBridgeTest/AtspiBridgeTestClient.c
 * The assistive-technology side of AtspiBridgeTest, written against libatspi
 * as Orca uses it: finds AtspiBridgeTestApp on the desktop, walks its tree,
 * reads the document through the Text interface, checks that the password
 * field is reported as one, moves the caret and checks the events the
 * application sends back. Exits 0 when every check passes.
 * Version: 1.2.0
 * Last Modified: 2026-10-08
 * Author: UltraCanvas Framework */

#include <atspi/atspi.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

static void check(const char* what, int ok) {
    checks++;
    if (!ok) failures++;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    fflush(stdout);
}

static int sawInsert = 0, sawDelete = 0, sawCaret = 0;

static void onEvent(AtspiEvent* event, void* data) {
    (void)data;
    const char* any = G_VALUE_HOLDS_STRING(&event->any_data) ? g_value_get_string(&event->any_data) : NULL;
    printf("EVENT %s %d %d \"%s\"\n", event->type, event->detail1, event->detail2, any ? any : "");
    if (!strcmp(event->type, "object:text-changed:insert") && event->detail1 == 8 && event->detail2 == 2 &&
        any && !strcmp(any, "XY")) sawInsert = 1;
    if (!strcmp(event->type, "object:text-changed:delete") && event->detail1 == 8 && event->detail2 == 2 &&
        any && !strcmp(any, "XY")) sawDelete = 1;
    if (!strcmp(event->type, "object:text-caret-moved") && event->detail1 == 8) sawCaret = 1;
    g_boxed_free(ATSPI_TYPE_EVENT, event);
    if (sawInsert && sawDelete) atspi_event_quit();
}

static gboolean giveUp(gpointer data) {
    (void)data;
    atspi_event_quit();
    return FALSE;
}

static AtspiAccessible* findRole(AtspiAccessible* node, AtspiRole role, int depth) {
    if (atspi_accessible_get_role(node, NULL) == role) return g_object_ref(node);
    if (depth > 10) return NULL;
    int count = atspi_accessible_get_child_count(node, NULL);
    for (int i = 0; i < count; i++) {
        AtspiAccessible* child = atspi_accessible_get_child_at_index(node, i, NULL);
        if (!child) continue;
        AtspiAccessible* found = findRole(child, role, depth + 1);
        g_object_unref(child);
        if (found) return found;
    }
    return NULL;
}

int main(void) {
    atspi_init();
    AtspiEventListener* listener = atspi_event_listener_new((AtspiEventListenerCB)onEvent, NULL, NULL);
    atspi_event_listener_register(listener, "object:text-changed", NULL);
    atspi_event_listener_register(listener, "object:text-caret-moved", NULL);

    AtspiAccessible* app = NULL;
    for (int tries = 0; tries < 100 && !app; tries++) {
        AtspiAccessible* desktop = atspi_get_desktop(0);
        int count = atspi_accessible_get_child_count(desktop, NULL);
        for (int i = 0; i < count && !app; i++) {
            AtspiAccessible* child = atspi_accessible_get_child_at_index(desktop, i, NULL);
            if (!child) continue;
            gchar* name = atspi_accessible_get_name(child, NULL);
            if (name && !strcmp(name, "AtspiBridgeTestApp")) app = g_object_ref(child);
            g_free(name);
            g_object_unref(child);
        }
        g_object_unref(desktop);
        if (!app) g_usleep(100000);
    }
    check("the application is on the desktop", app != NULL);
    if (!app) return 1;

    gchar* toolkit = atspi_accessible_get_toolkit_name(app, NULL);
    check("its toolkit is UltraCanvas", toolkit && !strcmp(toolkit, "UltraCanvas"));
    check("it is an application", atspi_accessible_get_role(app, NULL) == ATSPI_ROLE_APPLICATION);

    AtspiAccessible* frame = findRole(app, ATSPI_ROLE_FRAME, 0);
    gchar* title = frame ? atspi_accessible_get_name(frame, NULL) : NULL;
    check("its window is a frame named after the title", title && !strcmp(title, "Accessibility Test"));

    AtspiAccessible* doc = findRole(app, ATSPI_ROLE_DOCUMENT_TEXT, 0);
    check("the editor is a document", doc != NULL);
    if (!doc) return 1;
    gchar* name = atspi_accessible_get_name(doc, NULL);
    check("named after the document's title", name && !strcmp(name, "Quarterly Report"));
    AtspiAccessible* parent = atspi_accessible_get_parent(doc, NULL);
    check("whose parent is the window", parent && atspi_accessible_get_role(parent, NULL) == ATSPI_ROLE_FRAME);

    AtspiStateSet* states = atspi_accessible_get_state_set(doc);
    check("editable and multi-line", atspi_state_set_contains(states, ATSPI_STATE_EDITABLE) &&
                                     atspi_state_set_contains(states, ATSPI_STATE_MULTI_LINE));
    check("showing", atspi_state_set_contains(states, ATSPI_STATE_SHOWING));

    /* A password field is password text - Orca then speaks no character
       typed into it - and offers nothing to read. */
    AtspiAccessible* secret = findRole(app, ATSPI_ROLE_PASSWORD_TEXT, 0);
    check("the password field is password text", secret != NULL);
    check("whose text cannot be read", secret && atspi_accessible_get_text_iface(secret) == NULL);

    /* The common widgets. libatspi caches names and states; each read after
       a change clears the cache first, as the change event would. */
    AtspiAccessible* button = findRole(app, ATSPI_ROLE_PUSH_BUTTON, 0);
    gchar* buttonName = button ? atspi_accessible_get_name(button, NULL) : NULL;
    check("a button is a push button named by its text", buttonName && !strcmp(buttonName, "Apply"));
    AtspiAction* press = button ? atspi_accessible_get_action_iface(button) : NULL;
    gchar* pressName = press ? atspi_action_get_action_name(press, 0, NULL) : NULL;
    check("...whose one action is press", press && atspi_action_get_n_actions(press, NULL) == 1 &&
                                          pressName && !strcmp(pressName, "press"));
    AtspiAccessible* label = findRole(app, ATSPI_ROLE_LABEL, 0);
    gchar* before = label ? atspi_accessible_get_name(label, NULL) : NULL;
    check("a label is named by its text", before && !strcmp(before, "Idle"));
    check("pressing the button clicks it", press && atspi_action_do_action(press, 0, NULL));
    if (label) atspi_accessible_clear_cache(label);
    gchar* after = label ? atspi_accessible_get_name(label, NULL) : NULL;
    check("...as the label it changes shows", after && !strcmp(after, "Applied"));

    AtspiAccessible* checkbox = findRole(app, ATSPI_ROLE_CHECK_BOX, 0);
    gchar* boxName = checkbox ? atspi_accessible_get_name(checkbox, NULL) : NULL;
    check("a checkbox is a check checkbox named by its label", boxName && !strcmp(boxName, "Wrap lines"));
    AtspiStateSet* boxStates = checkbox ? atspi_accessible_get_state_set(checkbox) : NULL;
    check("...checkable and unchecked", boxStates && atspi_state_set_contains(boxStates, ATSPI_STATE_CHECKABLE) &&
                                        !atspi_state_set_contains(boxStates, ATSPI_STATE_CHECKED));
    AtspiAction* toggle = checkbox ? atspi_accessible_get_action_iface(checkbox) : NULL;
    check("its action ticks it", toggle && atspi_action_do_action(toggle, 0, NULL));
    if (checkbox) atspi_accessible_clear_cache(checkbox);
    boxStates = checkbox ? atspi_accessible_get_state_set(checkbox) : NULL;
    check("...and it is then checked", boxStates && atspi_state_set_contains(boxStates, ATSPI_STATE_CHECKED));

    AtspiAccessible* slider = findRole(app, ATSPI_ROLE_SLIDER, 0);
    gchar* sliderName = slider ? atspi_accessible_get_name(slider, NULL) : NULL;
    check("a slider is a slider with the name it was given", sliderName && !strcmp(sliderName, "Volume"));
    AtspiValue* value = slider ? atspi_accessible_get_value_iface(slider) : NULL;
    check("...whose value and range can be read", value && atspi_value_get_current_value(value, NULL) == 40.0 &&
          atspi_value_get_minimum_value(value, NULL) == 0.0 && atspi_value_get_maximum_value(value, NULL) == 100.0);
    check("...and set", value && atspi_value_set_current_value(value, 70.0, NULL) &&
                        atspi_value_get_current_value(value, NULL) == 70.0);

    AtspiAccessible* field = findRole(app, ATSPI_ROLE_ENTRY, 0);
    gchar* fieldName = field ? atspi_accessible_get_name(field, NULL) : NULL;
    check("a text field is an entry named by its placeholder", fieldName && !strcmp(fieldName, "Name"));
    AtspiText* fieldText = field ? atspi_accessible_get_text_iface(field) : NULL;
    gchar* typed = fieldText ? atspi_text_get_text(fieldText, 0, -1, NULL) : NULL;
    check("...whose text can be read", typed && !strcmp(typed, "Ada"));

    AtspiText* text = atspi_accessible_get_text_iface(doc);
    check("it has the Text interface", text != NULL);
    if (!text) return 1;
    gchar* all = atspi_text_get_text(text, 0, -1, NULL);
    check("its text is the paragraphs, a line each",
          all && !strcmp(all, "Report\nHello bold world. Second sentence here."));
    check("character count", atspi_text_get_character_count(text, NULL) == 46);

    AtspiTextRange* word = atspi_text_get_string_at_offset(text, 15, ATSPI_TEXT_GRANULARITY_WORD, NULL);
    check("the word at an offset", word && !strcmp(word->content, "bold ") && word->start_offset == 13);
    AtspiTextRange* sentence = atspi_text_get_string_at_offset(text, 15, ATSPI_TEXT_GRANULARITY_SENTENCE, NULL);
    check("the sentence at an offset", sentence && strstr(sentence->content, "Hello bold world.") == sentence->content);
    gint start = 0, end = 0;
    GHashTable* attributes = atspi_text_get_text_attributes(text, 15, &start, &end, NULL);
    const char* weight = attributes ? g_hash_table_lookup(attributes, "weight") : NULL;
    check("bold text reports its weight", weight && !strcmp(weight, "700") && start == 13 && end == 17);
    GHashTable* heading = atspi_text_get_text_attributes(text, 1, &start, &end, NULL);
    const char* level = heading ? g_hash_table_lookup(heading, "heading-level") : NULL;
    check("a heading reports its level", level && !strcmp(level, "1"));

    AtspiRect* box = atspi_text_get_character_extents(text, 0, ATSPI_COORD_TYPE_WINDOW, NULL);
    AtspiComponent* component = atspi_accessible_get_component_iface(doc);
    AtspiRect* extents = component ? atspi_component_get_extents(component, ATSPI_COORD_TYPE_WINDOW, NULL) : NULL;
    check("the document's extents in the window", extents && extents->x == 10 && extents->y == 10 &&
                                                  extents->width == 580 && extents->height == 380);
    check("the first character lies inside the document", box && extents && box->width > 0 && box->height > 0 &&
          box->x >= extents->x && box->y >= extents->y);
    gint at = box ? atspi_text_get_offset_at_point(text, box->x + box->width / 2, box->y + box->height / 2,
                                                   ATSPI_COORD_TYPE_WINDOW, NULL) : -1;
    printf("character 0 box %d,%d %dx%d; offset at its centre %d\n", box ? box->x : -1, box ? box->y : -1, box ? box->width : -1, box ? box->height : -1, at);
    check("and hit-testing its box finds it again", at == 0);

    /* Moving the caret makes the application type "XY" there and delete it. */
    gboolean moved = atspi_text_set_caret_offset(text, 8, NULL);
    check("the caret can be moved", moved && atspi_text_get_caret_offset(text, NULL) == 8);
    g_timeout_add(6000, giveUp, NULL);
    atspi_event_main();
    check("the caret move is announced", sawCaret);
    check("typing is announced as an insertion of \"XY\" at 8", sawInsert);
    check("deleting is announced as a deletion of \"XY\" at 8", sawDelete);

    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
