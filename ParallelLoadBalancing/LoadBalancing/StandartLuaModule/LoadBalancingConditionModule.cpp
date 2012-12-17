// ****************************************************************************
// LOAD BALANCING
// Copyright (C) 2012 Gerasimov, Smoryakova, Katerov, Afanasov, Kulakovich, Sobolev
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
// ****************************************************************************

#include <LoadBalancing/LuaAPI/ILoadBalancingCondition.h>
#include "../LoadBalancingCondition.h"
#include "LoadBalancingConditionModule.h"

static void LoadBalancingCondition_destructor(ILoadBalancingCondition* instance)
{
	delete static_cast<LoadBalancingCondition*>(instance);
}

static int luaModule_new(lua_State* L)
{
	int count = luaL_checkinteger(L, 1);

	luaLB_pushILoadBalancingCondition(L, new LoadBalancingCondition(count), LoadBalancingCondition_destructor);

	return 1;
}

static const luaL_Reg module_functions[] = {
	{"new", luaModule_new},
	{NULL,  NULL}
};

int luaopen_Standart_LoadBalancingCondition(lua_State* L)
{
	luaL_newlib(L, module_functions);
	return 1;
}