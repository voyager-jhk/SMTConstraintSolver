#include "smt_lang.h"
#include "smt_lang.tab.h"
#include "smt_lang_flex.h"

// Added for interval_solver
#include <limits.h> // For LLONG_MIN, LLONG_MAX
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Interval Structure ---
typedef struct {
    long long lower;
    long long upper;
} Interval;

// Global constants for intervals
const Interval INF_INTERVAL = {LLONG_MIN, LLONG_MAX};
const Interval EMPTY_INTERVAL = {1, 0}; // Canonical empty: lower > upper

// --- Map Structures (Simplified Array-Based) ---
// For SMT_VarName -> Interval mapping
typedef struct {
    char* name;
    Interval interval;
    bool active; // Indicates if this slot is used
} VarIntervalEntry;

// For SmtTerm* -> Interval mapping
typedef struct {
    SmtTerm* term_ptr;
    Interval interval;
    bool active; // Indicates if this slot is used
} TermIntervalEntry;

#define MAX_VARS_MAP 100
#define MAX_TERMS_MAP 500
VarIntervalEntry g_var_map[MAX_VARS_MAP];
int g_var_map_count = 0;
TermIntervalEntry g_term_map[MAX_TERMS_MAP];
int g_term_map_count = 0;

// --- Helper Functions for Intervals & Maps ---

long long safe_multiply(long long a, long long b) {
    if (a == 0 || b == 0) return 0;
    if (a == 1) return b;
    if (b == 1) return a;

    // Handing case -1
    if (a == -1) {
        if (b == LLONG_MIN) return LLONG_MAX; // -1 * LLONG_MIN --> LLONG_MAX
        return -b;
    }
    if (b == -1) {
        if (a == LLONG_MIN) return LLONG_MAX; // -1 * LLONG_MIN --> LLONG_MAX
        return -a;
    }

    // Handling cases where an operand is an extreme value
    if (a == LLONG_MAX) {
        if (b > 0) return LLONG_MAX;
        if (b < 0) return LLONG_MIN; // LLONG_MAX * negative
    }
    if (a == LLONG_MIN) {
        if (b > 0) return LLONG_MIN; // LLONG_MIN * positive (b!=1, b!=-1)
        if (b < 0) return LLONG_MAX; // LLONG_MIN * negative
    }
    // Symmetric treatment b is the extreme value
    if (b == LLONG_MAX) {
        if (a > 0) return LLONG_MAX;
        if (a < 0) return LLONG_MIN;
    }
    if (b == LLONG_MIN) {
        if (a > 0) return LLONG_MIN;
        if (a < 0) return LLONG_MAX;
    }

    // Overflow checking in normal circumstances
    // a > 0, b > 0:  a * b > LLONG_MAX  iff  a > LLONG_MAX / b
    if (a > 0 && b > 0) {
        if (a > LLONG_MAX / b) return LLONG_MAX;
    }
    // a < 0, b < 0:  a * b > LLONG_MAX  iff  (-a) * (-b) > LLONG_MAX
    // Need to be careful as a or b could be LLONG_MIN, then -a or -b overflows.
    else if (a < 0 && b < 0) {
        if (a == LLONG_MIN || b == LLONG_MIN) { // If one is LLONG_MIN, the other negative non -1
             // LLONG_MIN * (negative other than -1) => results in value > LLONG_MAX
            return LLONG_MAX;
        }
        // Now a and b are negative, but not LLONG_MIN
        if ((-a) > LLONG_MAX / (-b)) return LLONG_MAX;
    }
    // a > 0, b < 0 (or vice versa): a * b < LLONG_MIN
    // a > 0, b < 0: a * b < LLONG_MIN iff b < LLONG_MIN / a
    else if (a > 0 && b < 0) { // mixed signs, result negative
        if (b < LLONG_MIN / a) return LLONG_MIN;
    }
    // a < 0, b > 0: a * b < LLONG_MIN iff a < LLONG_MIN / b
    else if (a < 0 && b > 0) { // mixed signs, result negative
        if (a < LLONG_MIN / b) return LLONG_MIN;
    }
    
    return a * b;
}

void init_maps() {
    g_var_map_count = 0;
    for (int i = 0; i < MAX_VARS_MAP; ++i) g_var_map[i].active = false;
    g_term_map_count = 0;
    for (int i = 0; i < MAX_TERMS_MAP; ++i) g_term_map[i].active = false;
}

Interval intersect_intervals(Interval i1, Interval i2) {
    Interval res;
    res.lower = (i1.lower > i2.lower) ? i1.lower : i2.lower;
    res.upper = (i1.upper < i2.upper) ? i1.upper : i2.upper;
    if (res.lower > res.upper) return EMPTY_INTERVAL;
    return res;
}

bool is_empty_interval(Interval i) {
    return i.lower > i.upper;
}

bool interval_equals(Interval i1, Interval i2) {
    return i1.lower == i2.lower && i1.upper == i2.upper;
}

// --- Map Access Functions (Simplified Array-Based) ---

// Get or Add for Variables
VarIntervalEntry* get_or_add_var_entry(const char* name) {
    for (int i = 0; i < g_var_map_count; ++i) {
        if (g_var_map[i].active && strcmp(g_var_map[i].name, name) == 0) {
            return &g_var_map[i];
        }
    }
    if (g_var_map_count < MAX_VARS_MAP) {
        VarIntervalEntry* new_entry = &g_var_map[g_var_map_count++];
        new_entry->name = (char*)name; // Assumes parser manages string lifetime
        new_entry->interval = INF_INTERVAL;
        new_entry->active = true;
        return new_entry;
    }
    fprintf(stderr, "Error: Variable map full.\n");
    exit(1); // Or handle error more gracefully
}

// Get or Add for Terms
TermIntervalEntry* get_or_add_term_entry(SmtTerm* term) {
    if (term == NULL) return NULL;
    for (int i = 0; i < g_term_map_count; ++i) {
        if (g_term_map[i].active && g_term_map[i].term_ptr == term) {
            return &g_term_map[i];
        }
    }
    if (g_term_map_count < MAX_TERMS_MAP) {
        TermIntervalEntry* new_entry = &g_term_map[g_term_map_count++];
        new_entry->term_ptr = term;
        if (term->type == SMT_ConstNum) {
            new_entry->interval = (Interval){term->term.ConstNum, term->term.ConstNum};
        } else {
            new_entry->interval = INF_INTERVAL;
        }
        new_entry->active = true;
        return new_entry;
    }
    fprintf(stderr, "Error: Term map full for term type %d.\n", term->type);
    exit(1); // Or handle error more gracefully
}

// Recursive function to collect all terms and variables
void collect_terms_and_vars_recursive(SmtTerm* term) {
    if (term == NULL) return;
    get_or_add_term_entry(term); // Ensure this term is in the map

    if (term->type == SMT_VarName) {
        get_or_add_var_entry(term->term.Variable);
    } else if (term->type == SMT_LiaBTerm || term->type == SMT_NiaBTerm) {
        collect_terms_and_vars_recursive(term->term.BTerm.t1);
        collect_terms_and_vars_recursive(term->term.BTerm.t2);
    } else if (term->type == SMT_LiaUTerm) {
        collect_terms_and_vars_recursive(term->term.UTerm.t);
    } else if (term->type == SMT_UFTerm) {
        if (term->term.UFTerm) {
            for (int i = 0; i < term->term.UFTerm->numArgs; ++i) {
                collect_terms_and_vars_recursive(term->term.UFTerm->args[i]);
            }
        }
    }
}

void populate_maps_from_proplist(SmtProplist* list) {
    init_maps();
    for (SmtProplist* current = list; current != NULL; current = current->next) {
        SmtProp* prop = current->prop;
        // We are interested in atomic propositions for interval solving
        if (prop->type == SMTAT_PROP_EQ || prop->type == SMTAT_PROP_LIA) {
            collect_terms_and_vars_recursive(prop->prop.Atomic_prop.term1);
            collect_terms_and_vars_recursive(prop->prop.Atomic_prop.term2);
        }
        // Extend if other prop types become relevant for interval constraints
    }
}


// --- Forward Interval Calculation for Expressions f(e1, e2) ---
Interval calculate_forward_op_interval(SmtTermBop op, Interval i1, Interval i2) {
    if (is_empty_interval(i1) || is_empty_interval(i2)) return EMPTY_INTERVAL;

    // Handle cases where one operand is fully infinite (can lead to INF_INTERVAL for many ops)
    bool i1_inf = (i1.lower == LLONG_MIN && i1.upper == LLONG_MAX);
    bool i2_inf = (i2.lower == LLONG_MIN && i2.upper == LLONG_MAX);

    Interval res = INF_INTERVAL; // Default
    long long v[4]; // For multiplication/division products

    switch (op) {
        case LIA_ADD:
            if (i1.lower == LLONG_MIN || i2.lower == LLONG_MIN) res.lower = LLONG_MIN;
            else res.lower = i1.lower + i2.lower; // Check for underflow if not LLONG_MIN
            if (i1.upper == LLONG_MAX || i2.upper == LLONG_MAX) res.upper = LLONG_MAX;
            else res.upper = i1.upper + i2.upper; // Check for overflow if not LLONG_MAX
            // Basic overflow/underflow checks (could be more robust)
            if (i1.lower > 0 && i2.lower > 0 && res.lower < 0 && i1.lower != LLONG_MIN && i2.lower != LLONG_MIN) res.lower = LLONG_MAX;
            if (i1.upper > 0 && i2.upper > 0 && res.upper < 0 && i1.upper != LLONG_MAX && i2.upper != LLONG_MAX) res.upper = LLONG_MAX;
            if (i1.lower < 0 && i2.lower < 0 && res.lower > 0 && i1.lower != LLONG_MIN && i2.lower != LLONG_MIN) res.lower = LLONG_MIN;
            if (i1.upper < 0 && i2.upper < 0 && res.upper > 0 && i1.upper != LLONG_MAX && i2.upper != LLONG_MAX) res.upper = LLONG_MIN;
            break;
        case LIA_MINUS:
            if (i1.lower == LLONG_MIN || i2.upper == LLONG_MAX) res.lower = LLONG_MIN;
            else res.lower = i1.lower - i2.upper;
            if (i1.upper == LLONG_MAX || i2.lower == LLONG_MIN) res.upper = LLONG_MAX;
            else res.upper = i1.upper - i2.lower;
            // Similar overflow/underflow checks
            break;
        case LIA_MULT:
            // bool i1_inf and i2_inf are defined before the switch
            if ((i1.lower == 0 && i1.upper == 0) || (i2.lower == 0 && i2.upper == 0)) {
                res = (Interval){0,0};
                break;
            }

            // Handling cases where at least one operand is (-inf, +inf)
    		if (i1_inf && i2_inf) { // (-inf,+inf) * (-inf,+inf)
        		res = INF_INTERVAL;
        		break;
    		}
    		if (i1_inf) { // i1 is (-inf,+inf), i2 is not [0,0] and not (-inf,+inf)
        		if (i2.lower > 0 || i2.upper < 0) {
            		res = INF_INTERVAL; // e.g. [-inf, +inf] * [2,3] -> [-inf, +inf]
        		} else {
            		res = INF_INTERVAL; // [-inf, +inf] * [-2,3] is still [-inf, +inf]
        		}
        		break;
    		}
    		if (i2_inf) { // i2 is (-inf,+inf), i1 is not [0,0] and not (-inf,+inf)
         		if (i1.lower > 0 || i1.upper < 0) {
            		res = INF_INTERVAL;
        		} else {
            		res = INF_INTERVAL;
        		}
        		break;
    		}

    		// Neither operand is (-inf, +inf) nor [0,0]
    		v[0] = safe_multiply(i1.lower, i2.lower);
    		v[1] = safe_multiply(i1.lower, i2.upper);
    		v[2] = safe_multiply(i1.upper, i2.lower);
    		v[3] = safe_multiply(i1.upper, i2.upper);

    		res.lower = v[0];
    		res.upper = v[0];
    		for (int i = 1; i < 4; ++i) {
        		if (v[i] < res.lower) res.lower = v[i];
        		if (v[i] > res.upper) res.upper = v[i];
    		}
			break;
        case LIA_DIV:
            if (i2.lower == 0 && i2.upper == 0) return EMPTY_INTERVAL; // Division by exactly zero
            if (i2.lower <= 0 && i2.upper >= 0) return INF_INTERVAL; // Divisor interval contains 0 (but isn't [0,0])
            if (i1.lower == 0 && i1.upper == 0) return (Interval){0,0}; // 0 / x = 0 (if x!=0)
            if (i1_inf && !i2_inf) return INF_INTERVAL; // INF / finite_non_zero = INF
            if (i1_inf && i2_inf) return INF_INTERVAL; // INF/INF indeterminate, for intervals often wide

            // Assuming i2 does not contain 0 and is not [0,0]
            v[0] = i1.lower / i2.lower; v[1] = i1.lower / i2.upper;
            v[2] = i1.upper / i2.lower; v[3] = i1.upper / i2.upper;
            res.lower = v[0]; res.upper = v[0];
            for (int i = 1; i < 4; ++i) { // Min/max of the four resulting points
                if (v[i] < res.lower) res.lower = v[i];
                if (v[i] > res.upper) res.upper = v[i];
            }
            break;
        case LIA_LSHIFT:
        case LIA_RSHIFT:
            if (i2.lower != i2.upper || i2.lower < 0 || i2.lower >= 63) { // Treat as complex/undefined if shift is not a small positive constant
                return (i1_inf || i2_inf) ? INF_INTERVAL : EMPTY_INTERVAL; // Or wide interval
            }
            long long shift_val = i2.lower;
            if (i1_inf) return INF_INTERVAL; // Shifting full infinity results in full infinity

            if (op == LIA_LSHIFT) {
                // Check for overflow before shifting
                if (i1.lower > (LLONG_MIN >> shift_val) && i1.lower < (LLONG_MAX >> shift_val)) res.lower = i1.lower << shift_val; else res.lower = (i1.lower > 0) ? LLONG_MAX : LLONG_MIN;
                if (i1.upper > (LLONG_MIN >> shift_val) && i1.upper < (LLONG_MAX >> shift_val)) res.upper = i1.upper << shift_val; else res.upper = (i1.upper > 0) ? LLONG_MAX : LLONG_MIN;
                 if (i1.lower > 0 && (LLONG_MAX >> shift_val) < i1.lower ) res.lower = LLONG_MAX;
                 if (i1.upper > 0 && (LLONG_MAX >> shift_val) < i1.upper ) res.upper = LLONG_MAX;
            } else { // LIA_RSHIFT
                res.lower = i1.lower >> shift_val;
                res.upper = i1.upper >> shift_val;
            }
            break;
        default:
            return INF_INTERVAL;
    }
    if (res.lower > res.upper && !(res.lower == 1 && res.upper ==0)) return EMPTY_INTERVAL;
    return res;
}

// --- Core Recursive Evaluation & Refinement Functions ---

bool eval_and_update_term_interval_recursive(SmtTerm* term, bool* changed_overall);

// Backward propagation: term_val = t1 op t2. Refine t1 and t2.
bool refine_children_intervals_recursive(SmtTerm* term, bool* changed_overall) {
    if (term == NULL) return false;

    TermIntervalEntry* parent_entry = get_or_add_term_entry(term);
    Interval parent_interval = parent_entry->interval;
    if (is_empty_interval(parent_interval)) return false; // Cannot refine from empty

    if (term->type == SMT_LiaBTerm || term->type == SMT_NiaBTerm) {
        SmtTerm* t1 = term->term.BTerm.t1;
        SmtTerm* t2 = term->term.BTerm.t2;
        SmtTermBop op = term->term.BTerm.op;

        TermIntervalEntry* t1_entry = get_or_add_term_entry(t1);
        TermIntervalEntry* t2_entry = get_or_add_term_entry(t2);
        Interval i1_current = t1_entry->interval;
        Interval i2_current = t2_entry->interval;

        Interval i1_refined_by_t2 = INF_INTERVAL;
        Interval i2_refined_by_t1 = INF_INTERVAL;

        // Example: parent = t1 + t2  => t1 = parent - t2, t2 = parent - t1
        if (op == LIA_ADD) {
            i1_refined_by_t2 = calculate_forward_op_interval(LIA_MINUS, parent_interval, i2_current);
            i2_refined_by_t1 = calculate_forward_op_interval(LIA_MINUS, parent_interval, i1_current);
        } else if (op == LIA_MINUS) { // parent = t1 - t2 => t1 = parent + t2, t2 = t1 - parent
            i1_refined_by_t2 = calculate_forward_op_interval(LIA_ADD, parent_interval, i2_current);
            i2_refined_by_t1 = calculate_forward_op_interval(LIA_MINUS, i1_current, parent_interval);
        }
        // TODO: Add backward rules for MULT, DIV, SHIFT (these are more complex)
        // For MULT: If parent = t1 * t2, then t1 = parent / t2. Division rules apply.
        // For DIV: If parent = t1 / t2, then t1 = parent * t2.

        Interval final_i1 = intersect_intervals(i1_current, i1_refined_by_t2);
        if (is_empty_interval(final_i1)) { t1_entry->interval = EMPTY_INTERVAL; *changed_overall = true; return true; }
        if (!interval_equals(i1_current, final_i1)) { t1_entry->interval = final_i1; *changed_overall = true; }
        if (refine_children_intervals_recursive(t1, changed_overall)) return true;
        if (eval_and_update_term_interval_recursive(t1, changed_overall)) return true; // Re-evaluate t1 if its children changed

        Interval final_i2 = intersect_intervals(i2_current, i2_refined_by_t1);
        if (is_empty_interval(final_i2)) { t2_entry->interval = EMPTY_INTERVAL; *changed_overall = true; return true; }
        if (!interval_equals(i2_current, final_i2)) { t2_entry->interval = final_i2; *changed_overall = true; }
        if (refine_children_intervals_recursive(t2, changed_overall)) return true;
        if (eval_and_update_term_interval_recursive(t2, changed_overall)) return true;


    } else if (term->type == SMT_LiaUTerm) {
        SmtTerm* child_u = term->term.UTerm.t;
        TermIntervalEntry* child_u_entry = get_or_add_term_entry(child_u);
        Interval child_u_current = child_u_entry->interval;
        Interval child_u_refined = INF_INTERVAL;

        if (term->term.UTerm.op == LIA_NEG) { // parent = -child => child = -parent
             child_u_refined.lower = (parent_interval.upper == LLONG_MAX) ? LLONG_MIN : -parent_interval.upper;
             child_u_refined.upper = (parent_interval.lower == LLONG_MIN) ? LLONG_MAX : -parent_interval.lower;
        }
        Interval final_child_u = intersect_intervals(child_u_current, child_u_refined);
        if (is_empty_interval(final_child_u)) { child_u_entry->interval = EMPTY_INTERVAL; *changed_overall = true; return true;}
        if (!interval_equals(child_u_current, final_child_u)) { child_u_entry->interval = final_child_u; *changed_overall = true;}
        if (refine_children_intervals_recursive(child_u, changed_overall)) return true;
        if (eval_and_update_term_interval_recursive(child_u, changed_overall)) return true;


    } else if (term->type == SMT_VarName) { // If a term is a variable, its interval is directly updated by map_update_var_interval
        VarIntervalEntry* var_entry = get_or_add_var_entry(term->term.Variable);
        Interval old_var_interval = var_entry->interval;
        Interval new_var_interval = intersect_intervals(old_var_interval, parent_interval); // parent_interval is I(term)
        if(is_empty_interval(new_var_interval)) { var_entry->interval = EMPTY_INTERVAL; *changed_overall = true; return true; }
        if(!interval_equals(old_var_interval, new_var_interval)) { var_entry->interval = new_var_interval; *changed_overall = true; }
    }
    return false; // No empty interval found in this path
}


// Forward evaluation
bool eval_and_update_term_interval_recursive(SmtTerm* term, bool* changed_overall) {
    if (term == NULL) return false;

    TermIntervalEntry* current_term_entry = get_or_add_term_entry(term);
    Interval old_interval_for_this_term = current_term_entry->interval;
    Interval computed_interval = INF_INTERVAL;

    switch (term->type) {
        case SMT_ConstNum:
            computed_interval = (Interval){term->term.ConstNum, term->term.ConstNum};
            break;
        case SMT_VarName: {
            VarIntervalEntry* var_entry = get_or_add_var_entry(term->term.Variable);
            computed_interval = var_entry->interval; // A variable's interval is taken directly
            break;
        }
        case SMT_LiaUTerm: {
            if (eval_and_update_term_interval_recursive(term->term.UTerm.t, changed_overall)) return true; // Child became empty
            TermIntervalEntry* child_u_entry = get_or_add_term_entry(term->term.UTerm.t);
            if (term->term.UTerm.op == LIA_NEG) {
                Interval child_i = child_u_entry->interval;
                if (is_empty_interval(child_i)) { computed_interval = EMPTY_INTERVAL; break; }
                computed_interval.lower = (child_i.upper == LLONG_MAX) ? LLONG_MIN : -child_i.upper;
                computed_interval.upper = (child_i.lower == LLONG_MIN) ? LLONG_MAX : -child_i.lower;
            } else { // Unknown Unary Op
                computed_interval = INF_INTERVAL;
            }
            break;
        }
        case SMT_LiaBTerm:
        case SMT_NiaBTerm: { // NiaBTerm covers MULT, DIV, SHIFT here as per problem description on f(e1,e2)
            if (eval_and_update_term_interval_recursive(term->term.BTerm.t1, changed_overall)) return true;
            if (eval_and_update_term_interval_recursive(term->term.BTerm.t2, changed_overall)) return true;

            TermIntervalEntry* t1_entry = get_or_add_term_entry(term->term.BTerm.t1);
            TermIntervalEntry* t2_entry = get_or_add_term_entry(term->term.BTerm.t2);
            computed_interval = calculate_forward_op_interval(term->term.BTerm.op, t1_entry->interval, t2_entry->interval);
            break;
        }
        case SMT_UFTerm: // Uninterpreted functions - interval is INF unless special handling (not in scope)
            computed_interval = INF_INTERVAL;
            // Could try to propagate children if args changed, but result is still INF
            if(term->term.UFTerm) {
                for(int i=0; i < term->term.UFTerm->numArgs; ++i) {
                     if (eval_and_update_term_interval_recursive(term->term.UFTerm->args[i], changed_overall)) return true;
                }
            }
            break;
        default: // SMT_VarNum etc.
            computed_interval = INF_INTERVAL;
    }

    Interval final_new_interval = intersect_intervals(old_interval_for_this_term, computed_interval);
    if (is_empty_interval(final_new_interval)) {
        current_term_entry->interval = EMPTY_INTERVAL;
        *changed_overall = true; // Emptiness is a change
        return true; // Empty interval detected for this term
    }
    if (!interval_equals(old_interval_for_this_term, final_new_interval)) {
        *changed_overall = true;
    }
    current_term_entry->interval = final_new_interval;
    return false; // Not empty at this node
}


// --- Main interval_solver Function ---
int interval_solver(SmtProplist* list) {
    if (list == NULL) return 0; // No propositions, no conflict.

    populate_maps_from_proplist(list);

    bool changed_in_iteration;
    int iterations = 0;
    const int MAX_ITERATIONS = 2 * (g_var_map_count + g_term_map_count) + 10; // Heuristic limit, at most #vars * range_size updates

    do {
        changed_in_iteration = false;
        iterations++;

        // Phase 1: Forward evaluation for all terms involved in propositions
        for (SmtProplist* current_p = list; current_p != NULL; current_p = current_p->next) {
            SmtProp* prop = current_p->prop;
            if (prop->type == SMTAT_PROP_EQ || prop->type == SMTAT_PROP_LIA) {
                if (eval_and_update_term_interval_recursive(prop->prop.Atomic_prop.term1, &changed_in_iteration)) return 1;
                if (eval_and_update_term_interval_recursive(prop->prop.Atomic_prop.term2, &changed_in_iteration)) return 1;
            }
        }

        // Phase 2: Apply relational constraints from propositions
        for (SmtProplist* current_p = list; current_p != NULL; current_p = current_p->next) {
            SmtProp* prop = current_p->prop;
            if (!(prop->type == SMTAT_PROP_EQ || prop->type == SMTAT_PROP_LIA)) continue;

            SmtTerm* t1_term = prop->prop.Atomic_prop.term1;
            SmtTerm* t2_term = prop->prop.Atomic_prop.term2;
            SmtBinPred rel_op = prop->prop.Atomic_prop.op;

            TermIntervalEntry* t1_entry = get_or_add_term_entry(t1_term);
            TermIntervalEntry* t2_entry = get_or_add_term_entry(t2_term);

            Interval i1_current = t1_entry->interval;
            Interval i2_current = t2_entry->interval;

            Interval i1_after_relation = i1_current;
            Interval i2_after_relation = i2_current;

            switch (rel_op) {
                case SMT_EQ:
                    Interval common = intersect_intervals(i1_current, i2_current);
                    i1_after_relation = common;
                    i2_after_relation = common;
                    break;
                case SMT_LT: // t1 < t2  => t1.upper <= t2.upper-1, t2.lower >= t1.lower+1
                    if (i2_current.upper != LLONG_MAX) i1_after_relation.upper = (i1_after_relation.upper < i2_current.upper - 1) ? i1_after_relation.upper : i2_current.upper - 1;
                    if (i1_current.lower != LLONG_MIN) i2_after_relation.lower = (i2_after_relation.lower > i1_current.lower + 1) ? i2_after_relation.lower : i1_current.lower + 1;
                    break;
                case SMT_LE: // t1 <= t2 => t1.upper <= t2.upper, t2.lower >= t1.lower
                    if (i2_current.upper != LLONG_MAX) i1_after_relation.upper = (i1_after_relation.upper < i2_current.upper) ? i1_after_relation.upper : i2_current.upper;
                    if (i1_current.lower != LLONG_MIN) i2_after_relation.lower = (i2_after_relation.lower > i1_current.lower) ? i2_after_relation.lower : i1_current.lower;
                    break;
                case SMT_GT: // t1 > t2 (equiv. t2 < t1)
                    if (i1_current.upper != LLONG_MAX) i2_after_relation.upper = (i2_after_relation.upper < i1_current.upper - 1) ? i2_after_relation.upper : i1_current.upper - 1;
                    if (i2_current.lower != LLONG_MIN) i1_after_relation.lower = (i1_after_relation.lower > i2_current.lower + 1) ? i1_after_relation.lower : i2_current.lower + 1;
                    break;
                case SMT_GE: // t1 >= t2 (equiv. t2 <= t1)
                    if (i1_current.upper != LLONG_MAX) i2_after_relation.upper = (i2_after_relation.upper < i1_current.upper) ? i2_after_relation.upper : i1_current.upper;
                    if (i2_current.lower != LLONG_MIN) i1_after_relation.lower = (i1_after_relation.lower > i2_current.lower) ? i1_after_relation.lower : i2_current.lower;
                    break;
                default: break;
            }

            i1_after_relation = intersect_intervals(i1_current, i1_after_relation); // Ensure it only narrows
            i2_after_relation = intersect_intervals(i2_current, i2_after_relation); // Ensure it only narrows

            if (is_empty_interval(i1_after_relation)) { t1_entry->interval = EMPTY_INTERVAL; changed_in_iteration = true; return 1; }
            if (!interval_equals(i1_current, i1_after_relation)) { t1_entry->interval = i1_after_relation; changed_in_iteration = true; }

            if (is_empty_interval(i2_after_relation)) { t2_entry->interval = EMPTY_INTERVAL; changed_in_iteration = true; return 1; }
            if (!interval_equals(i2_current, i2_after_relation)) { t2_entry->interval = i2_after_relation; changed_in_iteration = true; }
        }

        // Phase 3: Backward propagation from terms to their children/variables
        for (SmtProplist* current_p = list; current_p != NULL; current_p = current_p->next) {
            SmtProp* prop = current_p->prop;
            if (prop->type == SMTAT_PROP_EQ || prop->type == SMTAT_PROP_LIA) {
                if (refine_children_intervals_recursive(prop->prop.Atomic_prop.term1, &changed_in_iteration)) return 1;
                if (refine_children_intervals_recursive(prop->prop.Atomic_prop.term2, &changed_in_iteration)) return 1;
            }
        }
        // After backward propagation, variables might have changed, which could affect terms containing them.
        // So, one more forward pass can be beneficial or simply rely on the next full iteration.
        // For simplicity, the next iteration's Phase 1 will handle this.

        if (iterations >= MAX_ITERATIONS) {
            printf("Warning: Max iterations reached, potential non-convergence or slow convergence.\n");
            break;
        }

    } while (changed_in_iteration);

    // One final check on all known variables and terms
    for(int i=0; i < g_var_map_count; ++i) if(g_var_map[i].active && is_empty_interval(g_var_map[i].interval)) return 1;
    for(int i=0; i < g_term_map_count; ++i) if(g_term_map[i].active && is_empty_interval(g_term_map[i].interval)) return 1;

    return 0; // No empty interval found
}

int main(int argc, char **argv) {
    char s[80] = "../test_example/test1.txt"; // Default input
    if (argc == 2) {
        printf("Manual decided input file: %s\n", argv[1]);
        strncpy(s, argv[1], sizeof(s) - 1);
        s[sizeof(s)-1] = '\0'; // Ensure null termination
    } else {
        printf("Using default input file: %s\n", s);
    }

    FILE *fp;
    fp = fopen(s, "rb"); // "rb" is unusual for text files, "r" is more common.
    if (fp == NULL) {
        perror("Error opening file");
        printf("File %s can't be opened.\n", s);
        exit(1);
    } else {
        yyin = fp;
    }

    // yydebug = 1; // Uncomment for bison debug output if compiled with debug support
    printf("\nSTARTING PARSING...\n");
    int parse_result = yyparse(); // Store result for checking
    extern struct SmtProplist* root;

    if (parse_result == 0) {
        printf("\nPARSING FINISHED SUCCESSFULLY.\n");
        if (root) {
            printf("Original Proplist:\n");
            printSmtProplist(root); // Assuming this prints original order
            root = reverseList(root); // As per original main
            printf("\nReversed Proplist for Solver:\n");
            printSmtProplist(root);
        } else {
            printf("Parsing successful, but root SmtProplist is NULL.\n");
        }
    } else {
        printf("\nPARSING FAILED with code: %d.\n", parse_result);
        fclose(fp);
        return 1; // Exit if parsing failed
    }

    printf("\nSTARTING INTERVAL SOLVER...\n");
    int res = interval_solver(root);
    printf("\nINTERVAL SOLVER FINISHED.\n");

    if (res == 1) {
        printf("Result: An empty interval was found (inconsistency detected).\n");
    } else {
        printf("Result: No empty interval found (consistent within interval arithmetic limits).\n");
    }
    
    // Print final intervals for debugging
    // printf("\n--- Final Variable Intervals ---\n");
    // for(int i=0; i < g_var_map_count; ++i) {
    //     if(g_var_map[i].active) {
    //         printf("Var '%s': [%lld, %lld]\n", g_var_map[i].name, g_var_map[i].interval.lower, g_var_map[i].interval.upper);
    //     }
    // }

    fclose(fp);

    printf("\nCleaning up AST...\n");
    if (root) {
        freeSmtProplist(root);
        root = NULL;
}

    return 0;
}