// © 2018 Joseph Cameron - All Rights Reserved
// Created on 2018-04-10.

#include <catch.hpp>


//#include <some/thing.h>
#include <string>
namespace some 
{
    struct thing 
    {
        std::string GetASecret(){return "This is a secret";}
    };
}
//===

TEST_CASE( "Some::other::thing test", "[Some::thing]" )
{
    some::thing a;

    SECTION("Some::Thing tells a secret")
    {
        std::string secret = a.GetASecret();

        REQUIRE( secret == "This is a secret" );
    }
}
