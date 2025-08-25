%{
	// this part is copied to the beginning of the parser 
	#include "smt_lang.h"
	#include <stdio.h>
	#include "smt_lang_flex.h"
	void yyerror(char*);
	int yylex(void);
	SmtProplist *root;
%}

%union {
    int n;
    char* s;
    struct SmtTerm* a;
    struct SmtProp* b;
    struct SmtProplist* c;
    void * none;
}

%token <none> LB1L LB1R COMMA

%token <s> TVAR 

%token <n> TNUM 

%token <none> TADD TMULT TDIV TMINUS LSHIFT RSHIFT

%token <none> RLE RLT RGE RGT REQ

%token <none> PAND POR PIFF PIMPLY PNOT FORALL EXISTS

%token <b> PTT PFF

%type <a> EXPR
%type <b> PROP
%type <c> PROP_LIST
%left TMINUS TADD LSHIFT RSHIFT
%left TMULT TDIV
%left PAND POR PIFF 
%right PIMPLY
%left PNOT
%%

PROP_LIST: PROP {
        printf("->PROP_LIST\n");
        SmtProplist* list = (SmtProplist*)malloc(sizeof(SmtProplist));
        printSmtProp($1);
        list->prop = $1;
        list->next = NULL;
        $$ = list;
        root = $$;
    }
    | PROP_LIST COMMA PROP {
        printf("->PROP_LIST\n");
        SmtProplist* list = (SmtProplist*)malloc(sizeof(SmtProplist));
        printSmtProp($3);
        list->prop = $3;
        list->next = $1;
        $$ = list;
        root = $$;
    }
;

PROP: PTT {
        printf("->PROP\n");
        $$ = $1;
    }
    | PFF {
        printf("->PROP\n");
        $$ = $1;
    }
    | LB1L PROP LB1R {
        printf("->PROP\n");
        $$ = $2;
    }
    | PNOT PROP {
        printf("->PROP\n");
        $$ = newSmtProp(SMTU_PROP, SMTPROP_NOT, $2, NULL, NULL, NULL, true);
    }
    | PROP PAND PROP {
        printf("->PROP\n");
        $$ = newSmtProp(SMTB_PROP, SMTPROP_AND, $1, $3, NULL, NULL, true);
    }
    | PROP POR PROP {
        printf("->PROP\n");
        $$ = newSmtProp(SMTB_PROP, SMTPROP_OR, $1, $3, NULL, NULL, true);
    }
    | PROP PIFF PROP {
        printf("->PROP\n");
        $$ = newSmtProp(SMTB_PROP, SMTPROP_IFF, $1, $3, NULL, NULL, true);
    }
    | PROP PIMPLY PROP {
        printf("->PROP\n");
        $$ = newSmtProp(SMTB_PROP, SMTPROP_IMPLY, $1, $3, NULL, NULL, true);
    }
    | LB1L FORALL TVAR COMMA PROP LB1R{
        printf("->PROP\n");
        SmtProp* res = (SmtProp*)malloc(sizeof(SmtProp));
        memset(res, 0, sizeof(SmtProp));
        res->type = SMT_QUANT_PROP;
        res->prop.Quant_prop.body = $5;
        res->prop.Quant_prop.type = Forall;
        res->prop.Quant_prop.quant_var = strdup($3);
        $$ = res;
    }
    | LB1L EXISTS TVAR COMMA PROP LB1R{
        printf("->PROP\n");
        SmtProp* res = (SmtProp*)malloc(sizeof(SmtProp));
        memset(res, 0, sizeof(SmtProp));
        res->type = SMT_QUANT_PROP;
        res->prop.Quant_prop.body = $5;
        res->prop.Quant_prop.type = Exists;
        res->prop.Quant_prop.quant_var = strdup($3);
        $$ = res;
    }
    | EXPR REQ EXPR {
        printf("->PROP\n");
        $$ = newSmtProp(SMTAT_PROP_EQ, SMT_EQ, NULL, NULL, $1, $3, true);
    }
    | EXPR RGE EXPR {
        printf("->PROP\n");
        $$ = newSmtProp(SMTAT_PROP_LIA, SMT_GE, NULL, NULL, $1, $3, true);
    }
    | EXPR RGT EXPR {
        printf("->PROP\n");
        $$ = newSmtProp(SMTAT_PROP_LIA, SMT_GT, NULL, NULL, $1, $3, true);
    }
    | EXPR RLE EXPR {
        printf("->PROP\n");
        $$ = newSmtProp(SMTAT_PROP_LIA, SMT_LE, NULL, NULL, $1, $3, true);
    }
    | EXPR RLT EXPR {
        printf("->PROP\n");
        $$ = newSmtProp(SMTAT_PROP_LIA, SMT_LT, NULL, NULL, $1, $3, true);
    }
;

EXPR: 
    TVAR {
        printf("->EXPR TVAR\n");
        $$ = newSmtTerm(SMT_VarName, 0, 0, $1, NULL, NULL, NULL);
    }
    |TNUM {
        printf("->EXPR TNUM\n");
        $$ = newSmtTerm(SMT_ConstNum, 0, $1, NULL, NULL, NULL, NULL);
    }
    |LB1L EXPR LB1R {
        printf("->EXPR\n");
        $$ = $2;
    }
    |EXPR TADD EXPR {
        printf("->EXPR\n");
        $$ = newSmtTerm(SMT_LiaBTerm, LIA_ADD, 0, NULL, NULL, $1, $3);
    }
    |EXPR TMINUS EXPR {
        printf("->EXPR\n");
        $$ = newSmtTerm(SMT_LiaBTerm, LIA_MINUS, 0, NULL, NULL, $1, $3);
    }
    |TMINUS EXPR {
        printf("->EXPR\n");
        $$ = newSmtTerm(SMT_LiaUTerm, LIA_NEG, 0, NULL, NULL, $2, NULL);
    }
    |EXPR TMULT EXPR {
        printf("->EXPR\n");
        $$ = newSmtTerm(SMT_NiaBTerm, LIA_MULT, 0, NULL, NULL, $1, $3);
    }
    |EXPR TDIV EXPR {
        printf("->EXPR\n");
        $$ = newSmtTerm(SMT_NiaBTerm, LIA_DIV, 0, NULL, NULL, $1, $3);
    }
    |EXPR LSHIFT EXPR {
        printf("->EXPR\n");
        $$ = newSmtTerm(SMT_NiaBTerm, LIA_LSHIFT, 0, NULL, NULL, $1, $3);
    }
    |EXPR RSHIFT EXPR {
        printf("->EXPR\n");
        $$ = newSmtTerm(SMT_NiaBTerm, LIA_RSHIFT, 0, NULL, NULL, $1, $3);
    }
    |TVAR LB1L EXPR LB1R {
        printf("->EXPR\n");
        UFunction* tmp = newUFunction($1, 1, $3, NULL, NULL);
        $$ = newSmtTerm(SMT_UFTerm, 0, 0, NULL, tmp, NULL, NULL);
    }
    |TVAR LB1L EXPR COMMA EXPR LB1R {
        printf("->EXPR\n");
        UFunction* tmp = newUFunction($1, 2, $3, $5, NULL);
         $$ = newSmtTerm(SMT_UFTerm, 0, 0, NULL, tmp, NULL, NULL);
    }
    |TVAR LB1L EXPR COMMA EXPR COMMA EXPR LB1R {
        printf("->EXPR\n");
        UFunction* tmp = newUFunction($1, 3, $3, $5, $7);
         $$ = newSmtTerm(SMT_UFTerm, 0, 0, NULL, tmp, NULL, NULL);
    }
;

%%

void yyerror(char* s)
{
    fprintf(stderr , "%s\n",s);
}