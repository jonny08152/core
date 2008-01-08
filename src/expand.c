/* 

        Copyright (C) 1994-
        Free Software Foundation, Inc.

   This file is part of GNU cfengine - written and maintained 
   by Mark Burgess, Dept of Computing and Engineering, Oslo College,
   Dept. of Theoretical physics, University of Oslo
 
   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any
   later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

*/

/*********************************************************************/
/*                                                                   */
/*  Variable expansion in cf3                                        */
/*                                                                   */
/*********************************************************************/

#include "cf3.defs.h"
#include "cf3.extern.h"

/*

Expanding variables is easy -- expanding lists automagically requires
some thought. Remember that

promiser <=> CF_SCALAR
promisee <=> CF_LIST

Expanding all bodies in the constraint list, we have

lval <=> CF_LIST|CF_SCALAR

Now the rule for variable substitution is that any list variable
substituted directly for a LIST is not iterated, but dropped into
place, i.e. in list-lvals and the promisee (since this would be
equivalent to a re-concatenation of the expanded separate promises)

Any list variable occuring within a scalar or in place of a scalar
is assumed to be iterated.

To expand a promise, we build temporary hash tables. There are two
stages, to this - one is to create a promise copy including all of the
body templates and translate the parameters. This requires one round
of expansion with scopeid "body". Then we use this fully assembled promise
and expand vars and function calls.

We should also exclude iteration as an error in lvals like "expireafter" etc,
or anything in a control body?.

*/

/*********************************************************************/

/* To expand the variables in a promise we need to 

   -- first get all strings, also parameterized bodies, which
      could also be lists
                                                                     /
        //  ScanRval("scope",&lol,"ksajd$(one)$(two)...$(three)"); \/ 
        
   -- compile an ordered list of variables involved , with types -           /
      assume all are lists - these are never inside sub-bodies by design,  \/
      so all expansion data are in the promise itself
      can also be variables based on list items - derived like arrays x[i]

   -- Copy the promise to a temporary promise + constraint list, expanding one by one,   /
      then execute that                                                                \/

      -- In a sub-bundle, create a new context and make hashes of the the
      transferred variables in the temporary context

   -- bodies cannot contain iterators

   -- we've already checked types of lhs rhs, must match so an iterator
      can only be in a non-naked variable?
   
   -- form the outer loops to generate combinations

**********************************************************************/

void ExpandPromise(char *scopeid,struct Promise *pp)

{ struct Rlist *rp, *listvars = NULL, *scalarvars = NULL;
  struct Constraint *cp;
  struct Promise *pcopy;
 
pcopy = DeRefCopyPromise(scopeid,pp);

ScanRval(scopeid,&scalarvars,&listvars,pcopy->promiser,CF_SCALAR);

if (pcopy->promisee != NULL)
   {
   ScanRval(scopeid,&scalarvars,&listvars,pp->promisee,pp->petype);
   }

for (cp = pcopy->conlist; cp != NULL; cp=cp->next)
   {
   ScanRval(scopeid,&scalarvars,&listvars,cp->rval,cp->type);
   }

ExpandPromiseAndDo(scopeid,pcopy,scalarvars,listvars);

DeletePromise(pcopy);
DeleteRlist(scalarvars);
DeleteRlist(listvars);
}

/*********************************************************************/

struct Rval ExpandDanglers(char *scopeid,struct Rval rval,struct Promise *pp)

{ struct Rval final;
  struct Rlist *rp;

 /* If there is still work left to do, expand and replace alloc */
 
switch (rval.rtype)
   {
   case CF_SCALAR:
       
       if (IsCf3VarString(rval.item))
          {
          final = EvaluateFinalRval(scopeid,rval.item,rval.rtype,false,pp);
          }
       else
          {
          final = rval;
          }
       break;

   default:
       final = rval;
       break;
   }

return final;
}

/*********************************************************************/

void ScanRval(char *scopeid,struct Rlist **scalarvars,struct Rlist **listvars,void *rval,char type)

{ struct Rlist *rp;
  struct FnCall *fp;
 
switch(type)
   {
   case CF_SCALAR:
       ScanScalar(scopeid,scalarvars,listvars,(char *)rval,0);
       break;
       
   case CF_LIST:
       for (rp = (struct Rlist *)rval; rp != NULL; rp=rp->next)
          {
          ScanRval(scopeid,scalarvars,listvars,rp->item,rp->type);
          }
       break;
       
   case CF_FNCALL:
       fp = (struct FnCall *)rval;
       
       for (rp = (struct Rlist *)fp->args; rp != NULL; rp=rp->next)
          {
          Debug("Looking at arg for function-like object %s()\n",fp->name);
          ScanScalar(scopeid,scalarvars,listvars,(char *)rp->item,0);
          }
       break;
   }
}

/*********************************************************************/

void ScanScalar(char *scopeid,struct Rlist **scal,struct Rlist **its,char *string,int level)

{ struct Rlist *rp;
  char *sp,rtype;
  void *rval;
  char var[CF_BUFSIZE],exp[CF_EXPANDSIZE];
  
Debug("ScanForVariables([%s])\n",string);

for (sp = string; (*sp != '\0') ; sp++)
   {
   var[0] = '\0';
   exp[0] = '\0';
   
   if (*sp == '$')
      {
      if (ExtractInnerVarString(sp,var))
         {
         if (GetVariable(scopeid,var,&rval,&rtype) != cf_notype)
            {
            if (rtype == CF_LIST)
               {
               Debug("List variable $(%s) found\n",var);
               ExpandScalar(var,exp);  // in touch with our inner string

               /* embedded iterators should be incremented fastest, so order list */
               
               if (level > 0)
                  {
                  IdempPrependRScalar(its,exp,CF_SCALAR);
                  }
               else
                  {
                  IdempAppendRScalar(its,exp,CF_SCALAR);
                  }
               }
            else if (rtype == CF_SCALAR)
               {
               Debug("Scalar variable $(%s) found\n",var);
               IdempAppendRScalar(scal,var,CF_SCALAR);
               }
            }
         else
            {
            Debug("Checking for nested vars, e.g. $(array[$(index)])....\n");
            
            if (IsExpandable(var))
               {
               Debug("Found embedded variables\n");
               ScanScalar(scopeid,scal,its,var,level+1);
               }
            }
         sp += strlen(var)-1;
         }
      }
   }
}

/*********************************************************************/

int ExpandScalar(char *string,char buffer[CF_EXPANDSIZE])

{
return ExpandPrivateScalar(CONTEXTID,string,buffer); 
}

/*********************************************************************/

struct Rlist *ExpandList(char *scopeid,struct Rlist *list)

{ struct Rlist *rp, *start = NULL;
  struct Rval returnval;
  char naked[CF_MAXVARSIZE];
  
for (rp = (struct Rlist *)list; rp != NULL; rp=rp->next)
   {
   if ((rp->type == CF_SCALAR) && IsNakedVar(rp->item,'@'))
      {
      GetNaked(naked,rp->item);
      
      if (GetVariable(scopeid,naked,&(returnval.item),&(returnval.rtype)) != cf_notype)
         {
         returnval = ExpandPrivateRval(scopeid,returnval.item,returnval.rtype);
         }
      else
         {
         returnval = ExpandPrivateRval(scopeid,rp->item,rp->type);
         }
      }
   else
      {
      returnval = ExpandPrivateRval(scopeid,rp->item,rp->type);
      }
   
   AppendRlist(&start,returnval.item,returnval.rtype);
   }

return start;
}

/*********************************************************************/

struct Rval ExpandPrivateRval(char *scopeid,void *rval,char type)
    
{ char buffer[CF_EXPANDSIZE];
 struct Rlist *rp, *start = NULL;
 struct FnCall *fp,*fpe;
 struct Rval returnval,extra;
     
Debug("ExpandPrivateRval(scope=%s,type=%c)\n",scopeid,type);

/* Allocates new memory for the copy */

returnval.item = NULL;
returnval.rtype = CF_NOPROMISEE;

switch (type)
   {
   case CF_SCALAR:

       ExpandPrivateScalar(scopeid,(char *)rval,buffer);
       returnval.item = strdup(buffer);
       returnval.rtype = CF_SCALAR;       
       break;
       
   case CF_LIST:

       returnval.item = ExpandList(scopeid,rval);
       returnval.rtype = CF_LIST;
       break;
       
   case CF_FNCALL:
       
       /* Note expand function does not mean evaluate function, must preserve type */
       fp = (struct FnCall *)rval;
       fpe = ExpandFnCall(scopeid,fp);

       returnval.item = fpe;
       returnval.rtype = CF_FNCALL;
       break;       
   }

return returnval;
}

/*********************************************************************/

int ExpandPrivateScalar(char *scopeid,char *string,char buffer[CF_EXPANDSIZE]) 

{ char *sp,rtype;
  void *rval;
  int varstring = false;
  char currentitem[CF_EXPANDSIZE],temp[CF_BUFSIZE],name[CF_MAXVARSIZE];
  int len,increment, returnval = true;
  time_t tloc;
  
memset(buffer,0,CF_EXPANDSIZE);

if (string == 0 || strlen(string) == 0)
   {
   return false;
   }

Debug("\nExpandPrivateScalar(%s,%s)\n",scopeid,string);

for (sp = string; /* No exit */ ; sp++)       /* check for varitems */
   {
   char var[CF_BUFSIZE];
   
   memset(var,0,CF_BUFSIZE);
   increment = 0;

   if (*sp == '\0')
      {
      break;
      }

   memset(currentitem,0,CF_EXPANDSIZE);
   
   sscanf(sp,"%[^$]",currentitem);
   
   if (ExpandOverflow(buffer,currentitem))
      {
      FatalError("Can't expand varstring");
      }
   
   strcat(buffer,currentitem);
   sp += strlen(currentitem);

   Debug("  Add |%s| to str, waiting at |%s|\n",buffer,sp);
   
   if (*sp == '\0')
      {
      break;
      }

   if (*sp == '$')
      {
      switch (*(sp+1))
         {
         case '(':
                   ExtractOuterVarString(sp,var);
                   varstring = ')';
                   break;
         case '{':
                   ExtractOuterVarString(sp,var);
                   varstring = '}';
                   break;

         default: 
                   strcat(buffer,"$");
                   continue;
         }
      }

   memset(currentitem,0,CF_EXPANDSIZE);

   temp[0] = '\0';
   ExtractInnerVarString(sp,temp);
   
   if (strstr(temp,"$"))
      {
      Debug("  Nested variables - %s\n",temp);
      ExpandVarstring(temp,currentitem,"");
      CheckVarID(currentitem);
      }
   else
      {
      strncpy(currentitem,temp,CF_BUFSIZE-1);
      }

   increment = strlen(var) - 1;
   Debug("  Scanning variable %s\n",currentitem);

   switch (GetVariable(scopeid,currentitem,&rval,&rtype))
      {
      case cf_str:
      case cf_int:
      case cf_real:
          
          if (ExpandOverflow(buffer,(char*)rval))
             {
             FatalError("Can't expand varstring");
             }
          
          strcat(buffer,(char *)rval);
          Debug("  Expansion gave (%s), len = %d\n",buffer,strlen(currentitem));
          break;

      case cf_slist:
      case cf_ilist:
      case cf_rlist:
      case cf_notype:
          Debug("  Currently non existent or list variable $(%s)\n",currentitem);
          
          if (varstring == '}')
             {
             snprintf(name,CF_MAXVARSIZE,"${%s}",currentitem);
             }
          else
             {
             snprintf(name,CF_MAXVARSIZE,"$(%s)",currentitem);
             }

          strcat(buffer,name);
          returnval = false;
          break;

      default:
          Debug("Returning Unknown Scalar (%s => %s)\n\n",string,buffer);
          return false;

      }
   
   sp += increment;
   currentitem[0] = '\0';
   }

if (returnval)
   {
   Debug("Returning complete scalar expansion (%s => %s)\n\n",string,buffer);

   /* Can we be sure this is complete? What about recursion */
   }
else
   {
   Debug("Returning partial / best effort scalar expansion (%s => %s)\n\n",string,buffer);
   }

return returnval;
}

/*********************************************************************/

void ExpandPromiseAndDo(char *scopeid,struct Promise *pp,struct Rlist *scalarvars,struct Rlist *listvars)

{ struct Rlist *lol = NULL; 
  struct Promise *pexp;
  struct Scope *ptr;
  int i = 1;

lol = NewIterationContext(scopeid,listvars);

do
   {
   if (lol && EndOfIteration(lol))
      {
      return;
      }
   
   DeRefListsInHashtable("this",listvars,lol);   
   pexp = ExpandDeRefPromise(scopeid,pp);

   // turn this into XML = 1, FOUT = expandfile

   fprintf(FOUT,"<p>");
   ShowPromise(pexp,6); // Delete me later, training pack only
   fprintf(FOUT,"</p>");
   
   DeletePromise(pexp);
   }
while (IncrementIterationContext(lol,1));

DeleteIterationContext(lol);
}

/*********************************************************************/

struct Rval EvaluateFinalRval(char *scopeid,void *rval,char rtype,int forcelist,struct Promise *pp)

{ struct Rlist *rp;
  struct Rval returnval,newret;
  char naked[CF_MAXVARSIZE];
  struct FnCall *fp;

Debug("EvaluateFinalRval\n");
  
if ((rtype == CF_SCALAR) && IsNakedVar(rval,'@')) /* Treat lists specially here */
   {
   GetNaked(naked,rval);
   
   if (GetVariable(scopeid,naked,&(returnval.item),&(returnval.rtype)) == cf_notype)
      {
      returnval = ExpandPrivateRval("this",rval,rtype);
      }
   else
      {
      returnval.item = ExpandList(scopeid,returnval.item);
      returnval.rtype = CF_LIST;
      }
   }
else
   {
   if (forcelist) /* We are replacing scalar @(name) with list */
      {
      returnval = ExpandPrivateRval(scopeid,rval,rtype);
      }
   else
      {
      returnval = ExpandPrivateRval("this",rval,rtype);
      }
   }

switch (returnval.rtype) 
   {
   case CF_SCALAR:
       break;
       
   case CF_LIST:
       for (rp = (struct Rlist *)returnval.item; rp != NULL; rp=rp->next)
          {
          if (rp->type == CF_FNCALL)
             {
             fp = (struct FnCall *)rp->item;
             newret = EvaluateFunctionCall(fp,pp);
             DeleteFnCall(fp);
             rp->item = newret.item;
             rp->type = newret.rtype;
             Debug("Replacing function call with new type (%c)\n",rp->type);
             }
          else
             {
             struct Scope *ptr = GetScope("this");

             if (ptr != NULL)
                {
                if (IsCf3VarString(rp->item))
                   {
                   newret = ExpandPrivateRval("this",rp->item,rp->type);
                   free(rp->item);
                   rp->item = newret.item;
                   }
                }
             }

          /* returnval unchanged */
          }
       break;
       
   case CF_FNCALL:
       // Also have to eval function now
       fp = (struct FnCall *)returnval.item;
       returnval = EvaluateFunctionCall(fp,pp);
       DeleteFnCall(fp);
       break;
   }

return returnval;
}

/*********************************************************************/
/* Tools                                                             */
/*********************************************************************/

int IsExpandable(char *str)

{ char *sp;
  char left = 'x', right = 'x';
  int dollar = false;
  int bracks = 0, vars = 0;

Debug1("IsExpandable(%s) - syntax verify\n",str);

for (sp = str; *sp != '\0' ; sp++)       /* check for varitems */
   {
   switch (*sp)
      {
      case '$':
          if (*(sp+1) == '{' || *(sp+1) == '(')
             {
             dollar = true;
             }
          break;
      case '(':
      case '{': 
          if (dollar)
             {
             left = *sp;    
             bracks++;
             }
          break;
      case ')':
      case '}': 
          if (dollar)
             {
             bracks--;
             right = *sp;
             }
          break;
      }
   
   if (left == '(' && right == ')' && dollar && (bracks == 0))
      {
      vars++;
      dollar=false;
      }
   
   if (left == '{' && right == '}' && dollar && (bracks == 0))
      {
      vars++;
      dollar = false;
      }
   }
 
 
if (bracks != 0)
   {
   Debug("If this is an expandable variable string then it contained syntax errors");
   return false;
   }

Debug("Found %d variables in (%s)\n",vars,str); 
return vars;
}

/*********************************************************************/

int IsNakedVar(char *str, char vtype)

{ char *sp,last = *(str+strlen(str)-1);
  int count=0;

  Debug1("IsNakedVar(%s,%c) - syntax verify for naked var substitution\n",str,vtype);

if (strlen(str) < 3)
   {
   return false;
   }

if (*str != vtype)
   {
   return false;
   }

switch (*(str+1))
   {
   case '(':
       if (last != ')')
          {
          return false;
          }
       break;
       
   case '{':
       if (last != '}')
          {
          return false;
          }
       break;       
   }


for (sp = str; *sp != '\0'; sp++)
   {
   switch (*sp)
      {
      case '(':
      case '{':
      case '[':
          count++;
          break;
      case ')':
      case '}':
      case ']':
          count--;
          break;
      }
   }

if (count != 0)
   {
   return false;
   }

return true;
}

/*********************************************************************/

void GetNaked(char *s2, char *s1)

/* copy @(listname) -> listname */
    
{
memset(s2,0,CF_MAXVARSIZE);
strncpy(s2,s1+2,strlen(s1)-3);
}
