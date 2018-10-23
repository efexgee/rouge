#include <time.h>
#include <stdlib.h>
#include <ncurses.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "level/level.h"
#include "mob/mob.h"
#include "simulation/simulation.h"
#include "los/los.h"
#include "color/color.h"

#define TILE_AIR_REGEN_THRESHOLD 20
#define TILE_AIR_REGEN_RATE 3

int keyboard_x = 0, keyboard_y = 0;
char message_banner[MESSAGE_LENGTH];

void draw_mobile(mobile *mob, int x_offset, int y_offset) {
    char icon = ((item*)mob)->display;

    if (mob->emote) {
        icon = mob->emote;
        mob->emote = false;
    }

    mvprintw(mob->y + y_offset, mob->x + x_offset, "%c", icon);
}

void draw(level *lvl) {
    int row,col;
    getmaxyx(stdscr,row,col);
    row -= 1;

    // Offset to keep player in center
    int x_offset = col / 2 - lvl->player->x;
    int y_offset = row / 2 - lvl->player->y;

    // Draw map and items
    for (int xx = 0; xx < col; xx++) {
        for (int yy = 0; yy < row; yy++) {
            // (xx,yy) are screen coordinates
            // (x ,y ) are map coordinates
            int x = xx - x_offset;
            int y = yy - y_offset;

            char icon = TILE_UNSEEN;

            if ((0 <= x && x < lvl->width) && (0 <= y && y < lvl->height)) {
                //TODO wrapper function with clear name
                if (can_see(lvl, lvl->player, x, y)) {
                    if (lvl->chemistry[x][y]->elements[fire] > 0) {
                        icon = STATUS_BURNING;
                        attron(COLOR_PAIR(RED));
                    } else if (lvl->items[x][y] != NULL) {
                        icon = lvl->items[x][y]->item->display;
                    } else {
                        icon = lvl->tiles[x][y];
                    }
                }
                // Fog of war
                if (icon == TILE_UNSEEN) {
                    icon = lvl->memory[x][y];
                    attron(COLOR_PAIR(YELLOW));
                } else {
                    lvl->memory[x][y] = icon;
                }
            }
            mvprintw(yy, xx, "%c", icon);
            //TODO this should be a general unsetter, not this
            attroff(COLOR_PAIR(YELLOW));
            attroff(COLOR_PAIR(RED));
        }
    }

    // Draw mobs
    for (int i=0; i < lvl->mob_count; i++) {
        mobile* mob = lvl->mobs[i];
        if (mob->active && can_see(lvl, lvl->player, mob->x, mob->y)) {
            if ((0 < mob->y + y_offset && mob->y + y_offset <= row) && (0 < mob->x + x_offset && mob->x + x_offset <= col)) {
                draw_mobile(mob, x_offset, y_offset);
            }
        }
    }
    draw_mobile(lvl->player, x_offset, y_offset);
    move(row, 0);
    clrtoeol();
    mvprintw(row, 0, message_banner);
}

//TODO When the same message (e.g. quaff) is repeated, it
//should be clear somehow that there were multiple messages
void print_message(char *msg) {
    strncpy(message_banner, msg, MESSAGE_LENGTH);
}

void drop_item(level *lvl, mobile *mob) {
    item *item = pop_inventory(mob);
    if (item != NULL) {
        level_push_item(lvl, item, mob->x, mob->y);
        if (mob == lvl->player) {
            char msg[MESSAGE_LENGTH];
            snprintf(msg, MESSAGE_LENGTH, "You drop the %s", item->name);
            print_message(msg);
        }
    }
}

void pickup_item(level *lvl, mobile *mob) {
    item *item = level_pop_item(lvl, mob->x, mob->y);
    if (item != NULL) {
        push_inventory(mob, item);
        if (mob == lvl->player) {
            char msg[MESSAGE_LENGTH];
            snprintf(msg, MESSAGE_LENGTH, "You pick up the %s", item->name);
            print_message(msg);
        }
    }
}

void smash_potion(level *lvl, mobile *mob) {
    item *potion = ((item*)mob)->contents->item;
    add_constituents(lvl->chemistry[mob->x][mob->y], potion->chemistry);
    inventory_item *inv = ((item*)mob)->contents;
    ((item*)mob)->contents = inv->next;
    free((void*)inv);
    destroy_item(potion);
}

void print_tile_elements(level *lvl, mobile *mob) {
    char *message = malloc(sizeof(char)*MESSAGE_LENGTH);
    constituents *chem = lvl->chemistry[mob->x][mob->y];
    snprintf(message, MESSAGE_LENGTH, "wood: %d air: %d fire: %d", chem->elements[wood], chem->elements[air], chem->elements[fire]);
    print_message(message);
    free((void*)message);
}

void toggle_door(level *lvl, mobile *mob) {
    int x = mob->x;
    int y = mob->y;
    switch (getch()) {
        case KEY_UP:
            y -= 1;
            break;
        case KEY_DOWN:
            y += 1;
            break;
        case KEY_RIGHT:
            x += 1;
            break;
        case KEY_LEFT:
            x -= 1;
            break;
        default:
            return;
    }
    if (lvl->tiles[x][y] == DOOR_OPEN) {
        lvl->tiles[x][y] = DOOR_CLOSED;
    } else if (lvl->tiles[x][y] == DOOR_CLOSED) {
        lvl->tiles[x][y] = DOOR_OPEN;
    }
}

int get_input(level *lvl) {
    //TODO Don't advance turn on any key press.
    //Have explicit "wait" key (e.g. <SPACE>)
    char *inventory;
    char *message = malloc(sizeof(char)*MESSAGE_LENGTH);
    int input = getch();
    int return_code = 0;

    switch (input) {
        case KEY_UP:
            lvl->keyboard_y = -1;
            break;
        case KEY_DOWN:
            lvl->keyboard_y = 1;
            break;
        case KEY_RIGHT:
            lvl->keyboard_x = 1;
            break;
        case KEY_LEFT:
            lvl->keyboard_x = -1;
            break;
        case 'H':
            snprintf(message, MESSAGE_LENGTH, "HELP: (i)nv (d)rop (.)pickup (r)otate inv (q)uaff (v)smash (e)xamine (s)tatus (t)ile (Q)uit");
            print_message(message);
            break;
        case 'i':
            inventory = inventory_string(lvl->player, MESSAGE_LENGTH);
            snprintf(message, MESSAGE_LENGTH, "Your inventory contains: %s", inventory);
            print_message(message);
            free((void*)inventory);
            break;
        case 'd':
            drop_item(lvl, lvl->player);
            break;
        case '.':
            pickup_item(lvl, lvl->player);
            break;
        case 'r':
            rotate_inventory(lvl->player);
            inventory = inventory_string(lvl->player, MESSAGE_LENGTH);
            snprintf(message, MESSAGE_LENGTH, "Currently selected item -> %s", inventory);
            print_message(message);
            free((void*)inventory);
            break;
        case 'q':
            if(quaff(lvl->player)) {
                print_message("You drink a potion.");
            } else {
                print_message("That isn't a potion.");
            }
            break;
        case 'v':
            if (((item*)lvl->player)->contents != NULL && ((item*)lvl->player)->contents->item->type == Potion) {
                smash_potion(lvl, lvl->player);
                print_message("You smash the potion on the floor.");
            } else print_message("That isn't a potion.");
            break;
        case 'e':
            if (((item*)lvl->player)->contents != NULL) {
                snprintf(message, MESSAGE_LENGTH, "wood: %d fire: %d ash: %d",
                        ((item*)lvl->player)->contents->item->chemistry->elements[wood],
                        ((item*)lvl->player)->contents->item->chemistry->elements[fire],
                        ((item*)lvl->player)->contents->item->chemistry->elements[ash]
                        );
                print_message(message);
            }
            break;
        case 's':
            snprintf(message, MESSAGE_LENGTH, "You have %d hit points. venom: %d banz: %d life: %d", ((item*)lvl->player)->health, ((item*)lvl->player)->chemistry->elements[venom], ((item*)lvl->player)->chemistry->elements[banz], ((item*)lvl->player)->chemistry->elements[life]);
            print_message(message);
            break;
        case 't':
            print_tile_elements(lvl, lvl->player);
            break;
        case 'o':
            toggle_door(lvl, lvl->player);
            break;
        case 'Q':
            return_code = -1;
    }
    free((void*)message);
    return return_code;
}

void step_chemistry(chemical_system *sys, constituents *chem, constituents *context) {
    for (int i = 0; i < 3; i++) {
        bool is_stable = chem->stable;
        if (context != NULL) is_stable = is_stable && context->stable;
        if (!is_stable) {
            react(sys, chem, context);
        }
    }
}

void step_inventory_chemistry(chemical_system *sys, inventory_item *inv, constituents *context) {
    while (inv != NULL) {
        step_chemistry(sys, inv->item->chemistry, context);
        inv = inv->next;
    }
}

void step_item(level *lvl, item *itm, constituents *chem_ctx) {
    step_chemistry(lvl->chem_sys, itm->chemistry, chem_ctx);
    step_inventory_chemistry(lvl->chem_sys, itm->contents, itm->chemistry);
    bool burning = itm->chemistry->elements[fire] > 0;
    if (chem_ctx != NULL) burning = burning || chem_ctx->elements[fire] > 0;
    if (burning && itm->health > 0) item_deal_damage(lvl, itm, 1);
}

void step_mobile(level *lvl, mobile *mob) {
    constituents *chemistry = ((item*)mob)->chemistry;
    if (lvl->chemistry[mob->x][mob->y]->elements[air] > 5) {
        lvl->chemistry[mob->x][mob->y]->elements[air] -= 5;
    } else {
        item_deal_damage(lvl, ((item*)mob), 1);
    }
    if (chemistry->elements[life] > 0) {
        chemistry->elements[life] -= 10;
        ((item*)mob)->health += 1;
    }
    if (chemistry->elements[venom] > 0) {
        chemistry->elements[venom] -= 10;
        item_deal_damage(lvl, ((item*)mob), 1);
    }
    step_item(lvl, (item*)mob, lvl->chemistry[mob->x][mob->y]);
    if (((item*)mob)->health <= 0) {
        mob->active = false;
    }
}

void level_step_chemistry(level* lvl) {
    for (int x = 0; x < lvl->width; x++) {
        for (int y = 0; y < lvl->height; y++) {
            step_chemistry(lvl->chem_sys, lvl->chemistry[x][y], NULL);
            inventory_item *inv = lvl->items[x][y];
            while (inv != NULL) {
                step_item(lvl, inv->item, lvl->chemistry[x][y]);
                if (inv->item->health <= 0) {
                    inv->item->name = "Ashy Remnants";
                    inv->item->display = ICON_ASH;
                }
                inv = inv->next;
            }
            if (lvl->tiles[x][y] != TILE_WALL && lvl->tiles[x][y] != DOOR_CLOSED && lvl->chemistry[x][y]->elements[air] < TILE_AIR_REGEN_THRESHOLD) lvl->chemistry[x][y]->elements[air] += TILE_AIR_REGEN_RATE;
        }
    }
    for (int element = 0; element < ELEMENT_COUNT; element++) {
        if (lvl->chem_sys->is_volatile[element]) {
            int **added_element = malloc(lvl->width * sizeof(int*));
            added_element[0] = malloc(lvl->height * lvl->width * sizeof(int));
            int **removed_element = malloc(lvl->width * sizeof(int*));
            removed_element[0] = malloc(lvl->height * lvl->width * sizeof(int));
            for(int i = 1; i < lvl->width; i++) {
                added_element[i] = added_element[0] + i * lvl->height;
                removed_element[i] = removed_element[0] + i * lvl->height;
            }
            for (int x = 0; x < lvl->width; x++) {
                for (int y = 0; y < lvl->height; y++) {
                    added_element[x][y] = 0;
                    removed_element[x][y] = 0;
                }
            }

            //TODO Make these variable names descriptive
            for (int x = 0; x < lvl->width; x++) {
                for (int y = 0; y < lvl->height; y++) {
                    int rx = rand();
                    int ry = rand();
                    for (int dx = 0; dx < 2; dx++) {
                        int xx = x + (((dx+rx)%3)-1);
                        for (int dy = 0; dy < 2; dy++) {
                            int yy = y + (((dy+ry)%3)-1);
                            if (xx >= 0 && xx < lvl->width && yy >= 0 && yy < lvl->height) {
                                if (lvl->tiles[xx][yy] != TILE_WALL && lvl->tiles[xx][yy] != DOOR_CLOSED && lvl->chemistry[x][y]->elements[element] - removed_element[x][y] > lvl->chemistry[xx][yy]->elements[element] + added_element[xx][yy]) {
                                    removed_element[x][y] += 1;
                                    added_element[xx][yy] += 1;
                                }
                            }
                        }
                    }
                }
            }

            for (int x = 0; x < lvl->width; x++) {
                for (int y = 0; y < lvl->height; y++) {
                    if (added_element[x][y] > 0 || removed_element[x][y] > 0) {
                        lvl->chemistry[x][y]->elements[element] += added_element[x][y] - removed_element[x][y];
                        lvl->chemistry[x][y]->stable = false;
                    }
                }
            }
            free((void*)added_element[0]);
            free((void*)added_element);
            free((void*)removed_element[0]);
            free((void*)removed_element);
        }
    }


}

int main() {
        int ch;
        int turn = 0;
        level *lvl;
        const char* do_log = getenv("ENABLE_LOG");

        if (do_log != NULL) logging_active = true;

        srand(time(NULL));
        initscr();

        if (! init_colors()) {
            //TODO print TERM environment variable
            logger("Terminal does not support color.\n");
            exit(1);
        }

        raw();
        noecho();
        cbreak();
        keypad(stdscr, TRUE);

        curs_set(0);

        lvl = make_level();
        draw(lvl);

        // Main Loop
        do {
            logger("=== Turn %3d ============================================\n", turn++);
            turn++;
            sync_simulation(lvl->sim, turn*TICKS_PER_TURN);

            for (int i=0; i < lvl->mob_count; i++) {
                if (lvl->mobs[i]->active) {
                    step_mobile(lvl, lvl->mobs[i]);
                }
            }

            // Update chemistry model
            level_step_chemistry(lvl);

            // Prep death message if player is dead
            if (!lvl->player->active) {
                print_message("You die");
            }

            // Update the screen
            draw(lvl);

            // If the player is dead, wait for input
            if (!lvl->player->active) {
                //TODO Require explicit input to really create a pause
                getch();
                break;
            }

            // Clear message
            print_message("");

        } while (get_input(lvl) == 0);

        destroy_level(lvl);
        endwin();
        return 0;
}
