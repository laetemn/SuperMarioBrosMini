#pragma bank 255

#include <string.h>

#include <gbdk/platform.h>
#include "system.h"
#include "vm.h"
#include "gbs_types.h"
#include "events.h"
#include "input.h"
#include "math.h"
#include "actor.h"
#include "scroll.h"
#include "game_time.h"
#include "actor_behavior.h"
#include "states/platform.h"
#include "states/playerstates.h"
#include "meta_tiles.h"
#include "collision.h"

#define BEHAVIOR_ACTIVATION_THRESHOLD 168
#define BEHAVIOR_DEACTIVATION_THRESHOLD 176
#define BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD -8

UBYTE actor_behavior_ids[MAX_ACTORS];
UBYTE actor_states[MAX_ACTORS];
WORD actor_vel_x[MAX_ACTORS];
WORD actor_vel_y[MAX_ACTORS];
UBYTE actor_counter_a[MAX_ACTORS];

WORD current_actor_x;
point16_t tmp_point;
const BYTE firebar_incx_lookup[] = { 0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3 };
const BYTE firebar_incy_lookup[] = { -8, -7, -6, -3, 0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7 };

void actor_behavior_init(void) BANKED {
    memset(actor_behavior_ids, 0, sizeof(actor_behavior_ids));
	memset(actor_states, 0, sizeof(actor_states));
	memset(actor_vel_x, 0, sizeof(actor_vel_x));
	memset(actor_vel_y, 0, sizeof(actor_vel_y));
	memset(actor_counter_a, 0, sizeof(actor_counter_a));
}

UWORD check_collision(UWORD start_x, UWORD start_y, bounding_box_t *bounds, col_check_dir_e check_dir) BANKED{
    WORD tx, ty;
    switch (check_dir) {
        case CHECK_DIR_LEFT:  // Check left (bottom left)
            tx = (((start_x >> 4) + bounds->left) >> 3);
            ty = (((start_y >> 4) + bounds->bottom) >> 3);
            if (tile_at(tx, ty) & COLLISION_RIGHT) {
                return ((tx + 1) << 7) - (bounds->left << 4);
            }
            return start_x;
        case CHECK_DIR_RIGHT:  // Check right (bottom right)
            tx = (((start_x >> 4) + bounds->right) >> 3);
            ty = (((start_y >> 4) + bounds->bottom) >> 3);
            if (tile_at(tx, ty) & COLLISION_LEFT) {
                return (tx << 7) - ((bounds->right + 1) << 4);
            }
            return start_x;
        case CHECK_DIR_UP:  // Check up (middle up)
            ty = (((start_y >> 4) + bounds->top) >> 3);
            tx = (((start_x >> 4) + ((bounds->left + bounds->right) >> 1)) >> 3);
            if (tile_at(tx, ty) & COLLISION_BOTTOM) {
                return ((ty + 1) << 7) - ((bounds->top) << 4);
            }
            return start_y;
        case CHECK_DIR_DOWN:  // Check down (right bottom and left bottom)
            ty = (((start_y >> 4) + bounds->bottom) >> 3);
            tx = (((start_x >> 4) + bounds->left) >> 3);
            if (tile_at(tx, ty) & COLLISION_TOP) {
                return ((ty) << 7) - ((bounds->bottom + 1) << 4);
            }			
			tx = (((start_x >> 4) + bounds->right) >> 3);
			if (tile_at(tx, ty) & COLLISION_TOP) {
                return ((ty) << 7) - ((bounds->bottom + 1) << 4);
            }
            return start_y;
    }
    return start_x;
}

UWORD check_pit(UWORD start_x, UWORD start_y, bounding_box_t *bounds, col_check_dir_e check_dir) BANKED {
     WORD tx, ty;
    switch (check_dir) {
        case CHECK_DIR_LEFT:  // Check left (bottom left)
            tx = (((start_x >> 4) + bounds->left) >> 3);
            ty = (((start_y >> 4) + bounds->bottom) >> 3) + 1;
            if (!(tile_at(tx, ty) & COLLISION_TOP)) {
                return ((tx + 1) << 7) - (bounds->left << 4);
            }
            return start_x;
        case CHECK_DIR_RIGHT:  // Check right (bottom right)
            tx = (((start_x >> 4) + bounds->right) >> 3);
            ty = (((start_y >> 4) + bounds->bottom) >> 3) + 1;
            if (!(tile_at(tx, ty) & COLLISION_TOP)) {
                return (tx << 7) - ((bounds->right + 1) << 4);
            }
            return start_x;       
    }
    return start_x;
}

void inline apply_gravity(UBYTE actor_idx) {
	actor_vel_y[actor_idx] += (plat_grav >> 8);
	actor_vel_y[actor_idx] = MIN(actor_vel_y[actor_idx], (plat_max_fall_vel >> 8));
}

void inline apply_velocity(UBYTE actor_idx, actor_t * actor) {
	//Apply velocity
	WORD new_y =  actor->pos.y + actor_vel_y[actor_idx];
	WORD new_x =  actor->pos.x + actor_vel_x[actor_idx];
	if (actor->collision_enabled){
		//Tile Collision
		actor->pos.x = check_collision(new_x, actor->pos.y, &actor->bounds, ((actor->pos.x > new_x) ? CHECK_DIR_LEFT : CHECK_DIR_RIGHT));
		if (actor->pos.x != new_x){
			actor_vel_x[actor_idx] = -actor_vel_x[actor_idx];
		}
		actor->pos.y = check_collision(actor->pos.x, new_y, &actor->bounds, ((actor->pos.y > new_y) ? CHECK_DIR_UP : CHECK_DIR_DOWN));
	} else {
		actor->pos.x = new_x;
		actor->pos.y = new_y;
	}
}

void inline apply_velocity_avoid_fall(UBYTE actor_idx, actor_t * actor) {
	//Apply velocity
	WORD new_y =  actor->pos.y + actor_vel_y[actor_idx];
	WORD new_x =  actor->pos.x + actor_vel_x[actor_idx];
	if (actor->collision_enabled){
		//Tile Collision
		new_x = check_collision(new_x, actor->pos.y, &actor->bounds, ((actor->pos.x > new_x) ? CHECK_DIR_LEFT : CHECK_DIR_RIGHT));
		actor->pos.x = check_pit(new_x, actor->pos.y, &actor->bounds, ((actor->pos.x > new_x) ? CHECK_DIR_LEFT : CHECK_DIR_RIGHT));
		if (actor->pos.x != new_x){
			actor_vel_x[actor_idx] = -actor_vel_x[actor_idx];
		}
		actor->pos.y = check_collision(actor->pos.x, new_y, &actor->bounds, ((actor->pos.y > new_y) ? CHECK_DIR_UP : CHECK_DIR_DOWN));
	} else {
		actor->pos.x = new_x;
		actor->pos.y = new_y;
	}
}

void actor_behavior_update(void) BANKED {
	for (UBYTE i = 1; i < MAX_ACTORS; i++){
		actor_t * actor = (actors + i);
		if (!actor->active){
			continue;
		}
		switch(actor_behavior_ids[i]){			
			case 1: //Goomba
			switch(actor_states[i]){
				case 0: //Init
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ actor_states[i] = 1; }
					break;
				case 1: //Main state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					apply_gravity(i);
					apply_velocity(i, actor);
					//Animation
					if (actor_vel_x[i] < 0) {
						actor_set_dir(actor, DIR_LEFT, TRUE);
					} else if (actor_vel_x[i] > 0) {
						actor_set_dir(actor, DIR_RIGHT, TRUE);
					} else {
						actor_set_anim_idle(actor);
					}
					break;
				case 255: //Deactivate
					deactivate_actor(actor);
					break;
			}		
			break;	
			case 2: //Koopa
			switch(actor_states[i]){
				case 0:
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ actor_states[i] = 1; }
					break;
				case 1: //Main state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					apply_gravity(i);
					apply_velocity(i, actor);
					//Animation
					if (actor_vel_x[i] < 0) {
						actor_set_dir(actor, DIR_LEFT, TRUE);
					} else if (actor_vel_x[i] > 0) {
						actor_set_dir(actor, DIR_RIGHT, TRUE);
					} else {
						actor_set_anim_idle(actor);
					}
					break;
				case 255:
					deactivate_actor(actor);
					break;
			}
			break;			
			case 3://Bowser
			switch(actor_states[i]){
				case 0: //Init
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ actor_states[i] = 1; }
					break;
				case 1: //Main state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					apply_gravity(i);
					apply_velocity(i, actor);
					//Animation
					if (PLAYER.pos.x < actor->pos.x) {
						actor_set_dir(actor, DIR_LEFT, TRUE);
					} else {
						actor_set_dir(actor, DIR_RIGHT, TRUE);
					}
					break;
				case 2: //Jump state
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) > BEHAVIOR_DEACTIVATION_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					actor_vel_y[i] += (plat_grav >> 10);
					apply_velocity(i, actor);
					//Animation
					if (PLAYER.pos.x < actor->pos.x) {
						actor_set_anim(actor, ANIM_JUMP_LEFT);
					} else {
						actor_set_anim(actor, ANIM_JUMP_RIGHT);
					}
					if (actor_vel_y[i] > 0){
						actor_states[i] = 1;
					}
					break;
				case 255: //Deactivate
					deactivate_actor(actor);
					break;
			}		
			break;	
			case 4://Bowser fire
			switch(actor_states[i]){
				case 0: //Init
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ actor_states[i] = 1; }
					break;
				case 1: //Main state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					actor->pos.x =  actor->pos.x + actor_vel_x[i];
					break;
				case 255: //Deactivate
					deactivate_actor(actor);
					break;
			}		
			break;			
			case 5://Pyrahna Plant
			switch(actor_states[i]){
				case 0: //Init
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ 
						actor_states[i] = 1; 
						actor_counter_a[i] = 60;
					}
					break;
				case 1: //Main state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					if (actor_vel_y[i] > 0){
						actor->pos.y += 16;
						actor_vel_y[i]--;
					}
					else if (((actor->pos.y >> 7) - 2) != PLAYER.pos.y >> 7){ //dont pop out if player is on top
						actor_counter_a[i]--;
						if (actor_counter_a[i] <= 0){
							actor_counter_a[i] = 120;
							actor_vel_y[i] = 16;
							actor_states[i] = 2; 
						}
					}
					break;
				case 2: //Out state
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) > BEHAVIOR_DEACTIVATION_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					if (actor_vel_y[i] > 0){
						actor->pos.y -= 16;
						actor_vel_y[i]--;
					}
					actor_counter_a[i]--;
					if (actor_counter_a[i] <= 0){
						actor_counter_a[i] = 180;
						actor_vel_y[i] = 16;
						actor_states[i] = 1; 
					}
					break;
				case 255: //Deactivate
					deactivate_actor(actor);
					break;
			}		
			break;
			case 6: //Red Koopa
			switch(actor_states[i]){
				case 0:
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ actor_states[i] = 1; }
					break;
				case 1: //Main state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					apply_gravity(i);
					apply_velocity_avoid_fall(i, actor);
					//Animation
					if (actor_vel_x[i] < 0) {
						actor_set_dir(actor, DIR_LEFT, TRUE);
					} else if (actor_vel_x[i] > 0) {
						actor_set_dir(actor, DIR_RIGHT, TRUE);
					} else {
						actor_set_anim_idle(actor);
					}
					break;
				case 255:
					deactivate_actor(actor);
					break;
			}
			break;	
			case 7: //Flying Red Koopa
			switch(actor_states[i]){
				case 0:
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ actor_states[i] = 1; }
					break;
				case 1: //Move up state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					
					if (actor_counter_a[i] < 120){
						actor_counter_a[i]++;
					} else {
						actor_states[i] = 2;
						actor_vel_y[i] = -actor_vel_y[i];
						actor_vel_x[i] = -actor_vel_x[i];
					}
					actor->pos.y = actor->pos.y + actor_vel_y[i];		
					actor->pos.x = actor->pos.x + actor_vel_x[i];						
					//Animation
					if (actor_vel_x[i] < 0) {
						actor_set_dir(actor, DIR_LEFT, TRUE);
					} else if (actor_vel_x[i] > 0) {
						actor_set_dir(actor, DIR_RIGHT, TRUE);
					} else {
						actor_set_anim_idle(actor);
					}
					break;
				case 2: //Move down state
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) > BEHAVIOR_DEACTIVATION_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					
					if (actor_counter_a[i] > 0){
						actor_counter_a[i]--;
					} else {
						actor_states[i] = 1;
						actor_vel_y[i] = -actor_vel_y[i];
						actor_vel_x[i] = -actor_vel_x[i];
					}
					actor->pos.y = actor->pos.y + actor_vel_y[i];
					actor->pos.x = actor->pos.x + actor_vel_x[i];	
					//Animation
					if (actor_vel_x[i] < 0) {
						actor_set_dir(actor, DIR_LEFT, TRUE);
					} else if (actor_vel_x[i] > 0) {
						actor_set_dir(actor, DIR_RIGHT, TRUE);
					} else {
						actor_set_anim_idle(actor);
					}
					break;
				case 255:
					deactivate_actor(actor);
					break;
			}
			break;	
			case 8://Fire bar
			switch(actor_states[i]){
				case 0: //Init
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ actor_states[i] = 1; }
					break;
				case 1: //Main state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}				
					if (!(game_time & 7)){
						actor_counter_a[i] = (actor_counter_a[i] + 1) & 15;
						actor->frame = actor->frame_start + actor_counter_a[i];
					}
					tmp_point.x = (actor->pos.x >> 4) + 4;
					tmp_point.y = (actor->pos.y >> 4) - 28;
					for (UBYTE j = 0; j < 4; j++){		
						if (bb_contains(&PLAYER.bounds, &PLAYER.pos, &tmp_point)){
							script_execute(actor->script.bank, actor->script.ptr, 0, 1, 0);
							break;
						}	
						tmp_point.x += firebar_incx_lookup[actor_counter_a[i]];
						tmp_point.y += firebar_incy_lookup[actor_counter_a[i]];
					}	
					break;
				case 255: //Deactivate
					deactivate_actor(actor);
					break;
			}		
			break;	
			case 9://Bouncing entity
			switch(actor_states[i]){
				case 0: //Init
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ actor_states[i] = 1; }
					break;
				case 1: //Main state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					actor_vel_y[i] += (plat_grav >> 10);
					actor_vel_y[i] = MIN(actor_vel_y[i], (plat_max_fall_vel >> 8));
					//Apply velocity
					WORD new_y =  actor->pos.y + actor_vel_y[i];
					WORD new_x =  actor->pos.x + actor_vel_x[i];
					//Tile Collision
					actor->pos.x = check_collision(new_x, actor->pos.y, &actor->bounds, ((actor->pos.x > new_x) ? CHECK_DIR_LEFT : CHECK_DIR_RIGHT));
					if (actor->pos.x != new_x){
						actor_vel_x[i] = -actor_vel_x[i];
					}
					actor->pos.y = check_collision(actor->pos.x, new_y, &actor->bounds, ((actor->pos.y > new_y) ? CHECK_DIR_UP : CHECK_DIR_DOWN));
					if (actor->pos.y < new_y){
						actor_vel_y[i] = -48;
					} else if (actor->pos.y > new_y){
						actor_vel_y[i] = 0;
					}
					//Animation
					if (actor_vel_x[i] < 0) {
						actor_set_dir(actor, DIR_LEFT, TRUE);
					} else if (actor_vel_x[i] > 0) {
						actor_set_dir(actor, DIR_RIGHT, TRUE);
					} else {
						actor_set_anim_idle(actor);
					}
					break;
				case 255: //Deactivate
					deactivate_actor(actor);
					break;
			}		
			break;	
			case 10: //Koopa shell
			switch(actor_states[i]){
				case 0:
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ actor_states[i] = 1; }
					break;
				case 1: //Main state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					apply_gravity(i);
					apply_velocity(i, actor);
					//Actor Collision					
					actor_t * hit_actor = actor_overlapping_bb(&actor->bounds, &actor->pos, actor, FALSE);
					if (hit_actor && hit_actor->script.bank){
						script_execute(hit_actor->script.bank, hit_actor->script.ptr, 0, 1, (UWORD)(actor->collision_group));
					}
					break;
				case 255:
					deactivate_actor(actor);
					break;
			}
			break;	
			case 11://Fire ball
			switch(actor_states[i]){
				case 0: //Init
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ actor_states[i] = 1; }
					break;
				case 1: //Main state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					actor_vel_y[i] += (plat_grav >> 9);
					actor_vel_y[i] = MIN(actor_vel_y[i], (plat_max_fall_vel >> 8));
					//Apply velocity
					WORD new_y =  actor->pos.y + actor_vel_y[i];
					WORD new_x =  actor->pos.x + actor_vel_x[i];
					//Tile Collision
					actor->pos.x = check_collision(new_x, actor->pos.y, &actor->bounds, ((actor->pos.x > new_x) ? CHECK_DIR_LEFT : CHECK_DIR_RIGHT));
					if (actor->pos.x != new_x){
						script_execute(actor->script.bank, actor->script.ptr, 0, 1, 2);
						actor_states[i] = 255;
						break;
					}
					actor->pos.y = check_collision(actor->pos.x, new_y, &actor->bounds, ((actor->pos.y > new_y) ? CHECK_DIR_UP : CHECK_DIR_DOWN));
					if (actor->pos.y < new_y){
						actor_vel_y[i] = -40;
					} else if (actor->pos.y > new_y){
						actor_vel_y[i] = 0;
					}
					//Actor Collision					
					actor_t * hit_actor = actor_overlapping_bb(&actor->bounds, &actor->pos, actor, FALSE);
					if (hit_actor && hit_actor->script.bank && actor->collision_group != hit_actor->collision_group){
						script_execute(hit_actor->script.bank, hit_actor->script.ptr, 0, 1, 2);
						script_execute(actor->script.bank, actor->script.ptr, 0, 1, 2);
						actor_states[i] = 255;
					}
					break;
				case 255: //Deactivate
					deactivate_actor(actor);
					break;
			}		
			break;		
			case 12: //Growing Beanstalk
			switch(actor_states[i]){
				case 0:
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ 
						actor_counter_a[i] = 0;
						actor_states[i] = 1; 
						actor->pos.y = (actor->pos.y >> 7) << 7;
						actor->pos.x = (actor->pos.x >> 7) << 7;
						replace_meta_tile((actor->pos.x >> 7), (actor->pos.y >> 7) - 1, 151);
					}
				case 1: //Move up state
					current_actor_x = ((actor->pos.x >> 4) + 8) - draw_scroll_x;
					if (current_actor_x > BEHAVIOR_DEACTIVATION_THRESHOLD || current_actor_x < BEHAVIOR_DEACTIVATION_LOWER_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					if (actor->pos.y > 384){
						actor->pos.y = actor->pos.y - 8;	
						actor_counter_a[i] = actor_counter_a[i] + 1;
						if (actor_counter_a[i] > 15){
							actor_counter_a[i] = 0;
							replace_meta_tile((actor->pos.x >> 7), (actor->pos.y >> 7) - 1, 151);
							if (tile_at((actor->pos.x >> 7), (actor->pos.y >> 7) - 2) & COLLISION_BOTTOM) {
								actor_states[i] = 255; 
							}
						}
					}	
					else{
						actor_states[i] = 255; 
					}
					break;
				case 255:
					actor_counter_a[i] = 0;
					deactivate_actor(actor);
					break;
			}
			break;
			case 13: //Moving platform (activates on player touch)
			switch(actor_states[i]){
				case 0:
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) < BEHAVIOR_ACTIVATION_THRESHOLD){ actor_states[i] = 1; }
				case 1: //Not moving state
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) > BEHAVIOR_DEACTIVATION_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}	
					if (actor_attached && last_actor == actor) {//start moving on player attach
						actor_states[i] = 2; 
					}					
					break;
				case 2: //moving state
					if ((((actor->pos.x >> 4) + 8) - draw_scroll_x) > BEHAVIOR_DEACTIVATION_THRESHOLD){ 
						actor_states[i] = 255; 
						break;
					}
					actor->pos.x = actor->pos.x + actor_vel_x[i];
					actor->pos.y = actor->pos.y + actor_vel_y[i];
					break;
				case 255:
					actor_counter_a[i] = 0;
					deactivate_actor(actor);
					break;
			}
			break;				
		}			
	}
}

void vm_set_actor_behavior(SCRIPT_CTX * THIS) OLDCALL BANKED {
    UBYTE actor_idx = *(uint8_t *)VM_REF_TO_PTR(FN_ARG0);
    UBYTE behavior_id = *(uint8_t *)VM_REF_TO_PTR(FN_ARG1);
    actor_behavior_ids[actor_idx] = behavior_id;
}

void vm_set_actor_state(SCRIPT_CTX * THIS) OLDCALL BANKED {
    UBYTE actor_idx = *(uint8_t *)VM_REF_TO_PTR(FN_ARG0);
    UBYTE state_id = *(uint8_t *)VM_REF_TO_PTR(FN_ARG1);
    actor_states[actor_idx] = state_id;
}

void vm_get_actor_state(SCRIPT_CTX * THIS) OLDCALL BANKED {
    UBYTE actor_idx = *(uint8_t *)VM_REF_TO_PTR(FN_ARG0);
	script_memory[*(int16_t*)VM_REF_TO_PTR(FN_ARG1)] = actor_states[actor_idx];
}

void vm_set_actor_velocity_x(SCRIPT_CTX * THIS) OLDCALL BANKED {
    UBYTE actor_idx = *(uint8_t *)VM_REF_TO_PTR(FN_ARG0);
    WORD vel_x = *(int16_t *)VM_REF_TO_PTR(FN_ARG1);
    actor_vel_x[actor_idx] = vel_x;
}

void vm_set_actor_velocity_y(SCRIPT_CTX * THIS) OLDCALL BANKED {
    UBYTE actor_idx = *(uint8_t *)VM_REF_TO_PTR(FN_ARG0);
    WORD vel_y = *(int16_t *)VM_REF_TO_PTR(FN_ARG1);
    actor_vel_y[actor_idx] = vel_y;
}