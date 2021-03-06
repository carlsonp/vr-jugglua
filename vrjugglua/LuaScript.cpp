/**	@file
        @brief	implementation

        @date
        2009-2011

        @author
        Ryan Pavlik
        <rpavlik@iastate.edu> and <abiryan@ryand.net>
        http://academic.cleardefinition.com/
        Iowa State University Virtual Reality Applications Center
        Human-Computer Interaction Graduate Program
*/

//          Copyright Iowa State University 2009-2011.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

// Internal Includes
#include "LuaScript.h"
#include "ApplyBinding.h"
#include "LuaGCBlock.h"
#include "LuaPath.h"

#include "VRJLuaOutput.h"

// Library/third-party includes
#include <vrjugglua/LuaIncludeFull.h>

#include <luabind/luabind.hpp>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

// Standard includes
#include <functional>
#include <iostream>
#include <sstream>

#ifdef VERBOSE
#define VERBOSE_MSG(X)                                                         \
    std::cout << "In " << __FUNCTION__ << " at " << __FILE__ << ":"            \
              << __LINE__ << " with this=" << this << ": " << X << std::endl
#else
#define VERBOSE_MSG(X)
#endif

namespace vrjLua {

    bool LuaScript::exitOnError = false;
    boost::function<void(std::string const &)> LuaScript::_printFunc;

    LuaScript::LuaScript(const bool create) {
        VERBOSE_MSG("Constructor");
        if (create) {
            VERBOSE_MSG("Create=true");
            _state = LuaStatePtr(luaL_newstate(), std::ptr_fun(lua_close));

            if (!_state) {
                throw CouldNotOpenState();
            }

            {
                /// RAII-style disabling of garbage collector during init
                LuaGCBlock gcBlocker(_state.get());
                // Load default Lua libs
                luaL_openlibs(_state.get());

                // Load our bindings.
                _applyBindings();
            }
        }
    }

    LuaScript::LuaScript(lua_State *state, bool bind)
        : _state(borrowStatePtr(state)) {
        VERBOSE_MSG("Constructor from lua_State * with bind = " << bind);
        if (!_state) {
            throw NoValidLuaState();
        }

        // If requested, bind.
        if (bind) {
            _applyBindings();
        }
    }

    LuaScript::LuaScript(const LuaScript &other) : _state(other._state) {
        VERBOSE_MSG("Copy constructor");
    }

    LuaScript::LuaScript(const LuaStatePtr &otherptr) : _state(otherptr) {
        VERBOSE_MSG("Constructor from LuaStatePtr (smart pointer)");
        if (!_state) {
            throw NoValidLuaState();
        }
    }

    LuaScript::~LuaScript() { VERBOSE_MSG("Destructor"); }

    LuaScript &LuaScript::operator=(const LuaScript &other) {
        if (!other._state) {
            std::cerr << "Warning: setting a LuaScript equal to another "
                         "LuaScript with an empty state smart pointer!"
                      << std::endl;
        }

        if (&other == this) {
            /// self assignment
            return *this;
        }

        _state = other._state;
        return *this;
    }

    void LuaScript::initWithArgv0(const char *argv0) {
        LuaPath::instance(argv0);
    }

    bool LuaScript::_handleLuaReturnCode(int returnVal,
                                         std::string const &failureMsg,
                                         std::string const &successMsg) {
        if (returnVal != 0) {
            std::string err;
            try {
                luabind::object o(luabind::from_stack(_state.get(), -1));
                err = "  Lua returned this error message: " +
                      luabind::object_cast<std::string>(o);
            }
            catch (...) {
                // do nothing - couldn't get an error string
                err = "  Furthermore, we couldn't get error details from Lua.";
            }

            doPrint(failureMsg + err);
            return false;
        } else {
            if (!successMsg.empty()) {
                VRJLUA_MSG_START(dbgVRJLUA, MSG_STATUS)
                    << successMsg << VRJLUA_MSG_END(dbgVRJLUA, MSG_STATUS);
            }
            return true;
        }
    }

    bool LuaScript::runFile(const std::string &fn, bool silentSuccess) {
        if (!_state) {
            throw NoValidLuaState();
        }

        return _handleLuaReturnCode(
            luaL_dofile(_state.get(), fn.c_str()),
            std::string("vrjLua ERROR: Could not run Lua file ") + fn + ".",
            silentSuccess ? ""
                          : std::string("Successfully ran Lua file ") + fn);
    }

    bool LuaScript::requireModule(const std::string &mod, bool silentSuccess) {
        if (!_state) {
            throw NoValidLuaState();
        }
        try {
            luabind::call_function<void>(_state.get(), "require", mod);
        }
        catch (std::exception &e) {
            doPrint(std::string("vrjLua ERROR: Could not load Lua module ") +
                    mod + " - error: " + e.what());
            return false;
        }
        if (!silentSuccess) {
            VRJLUA_MSG_START(dbgVRJLUA, MSG_STATUS)
                << "Successfully loaded Lua module " << mod
                << VRJLUA_MSG_END(dbgVRJLUA, MSG_STATUS);
        }
        return true;
    }

    bool LuaScript::runString(const std::string &str, bool silentSuccess) {
        if (!_state) {
            throw NoValidLuaState();
        }

        return _handleLuaReturnCode(
            luaL_dostring(_state.get(), str.c_str()),
            "vrjLua ERROR: Could not run provided Lua string.",
            silentSuccess ? "" : "Lua string executed successfully.");
    }

    void LuaScript::_applyBindings() {
        if (!_state) {
            throw NoValidLuaState();
        }
        applyBinding(_state.get());
    }

    bool LuaScript::call(const std::string &func, bool silentSuccess) {
        if (!_state) {
            throw NoValidLuaState();
        }
        try {
            return luabind::call_function<bool>(_state.get(), func.c_str());
        }
        catch (const std::exception &e) {
            std::cerr << "Caught exception calling '" << func
                      << "': " << e.what() << std::endl;
            throw;
        }
    }

    void LuaScript::setPrintFunction(
        boost::function<void(std::string const &)> func) {
        _printFunc = func;
    }

    LuaStatePtr const &LuaScript::getLuaState() const {
        if (!_state) {
            throw NoValidLuaState();
        }
        return _state;
    }

    lua_State *LuaScript::getLuaRawState() const {
        if (!_state) {
            throw NoValidLuaState();
        }
        return _state.get();
    }

    void LuaScript::doPrint(std::string const &str) {
        if (_printFunc) {
            _printFunc(str);
        } else {
            VRJLUA_MSG_START(dbgVRJLUA_APP, MSG_STATUS)
                << str << VRJLUA_MSG_END(dbgVRJLUA_APP, MSG_STATUS);
        }
    }
} // end of vrjLua namespace
