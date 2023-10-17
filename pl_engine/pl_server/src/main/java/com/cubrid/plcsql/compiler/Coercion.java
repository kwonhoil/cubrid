/*
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

package com.cubrid.plcsql.compiler;

import com.cubrid.plcsql.compiler.ast.TypeSpec;
import com.cubrid.plcsql.compiler.ast.TypeSpecPercent;
import com.cubrid.plcsql.compiler.ast.TypeSpecSimple;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

public abstract class Coercion {

    public abstract String javaCode(String exprJavaCode);

    public static Coercion getCoercion(TypeSpec from, TypeSpec to) {

        if (from instanceof TypeSpecPercent) {
            from = ((TypeSpecPercent) from).resolvedType;
            assert from != null;
        }
        if (to instanceof TypeSpecPercent) {
            to = ((TypeSpecPercent) to).resolvedType;
            assert to != null;
        }

        if (from.equals(to)) {
            return IDENTITY;
        } else if (from.equals(TypeSpecSimple.NULL)) {
            // why NULL?: in order for Javac to pick the right version among operator function
            // overloads when all the arguments are nulls
            return new Cast(to);
        } else if (to.equals(TypeSpecSimple.OBJECT)) {
            return IDENTITY;
        }

        Set<TypeSpec> possibleTargets = possibleCasts.get(from);
        if (possibleTargets != null && possibleTargets.contains(to)) {
            return new Conversion(from.pcsName, to.pcsName);
        } else {
            return null;
        }
    }

    // ----------------------------------------------
    // cases
    // ----------------------------------------------

    public static class Identity extends Coercion {
        @Override
        public String javaCode(String exprJavaCode) {
            return exprJavaCode; // no coercion
        }
    }

    public static Coercion IDENTITY = new Identity();

    public static class Cast extends Coercion {
        public TypeSpec to;

        public Cast(TypeSpec to) {
            this.to = to;
        }

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format("(%s) %s", to.javaCode(), exprJavaCode);
        }
    }

    public static class Conversion extends Coercion {
        public String from;
        public String to;

        public Conversion(String from, String to) {
            this.from = from;
            this.to = to;
        }

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format("conv%sTo%s(%s)", from, to, exprJavaCode);
        }
    }

    // ----------------------------------------------
    // Private
    // ----------------------------------------------

    private static final Map<TypeSpec, Set<TypeSpec>> possibleCasts = new HashMap<>();

    static {
        possibleCasts.put(
                TypeSpecSimple.DATETIME,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.DATE,
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.STRING)));
        possibleCasts.put(
                TypeSpecSimple.DATE,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.DATETIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.STRING)));
        possibleCasts.put(TypeSpecSimple.TIME, new HashSet(Arrays.asList(TypeSpecSimple.STRING)));
        possibleCasts.put(
                TypeSpecSimple.TIMESTAMP,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.DATETIME,
                                TypeSpecSimple.DATE,
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.STRING)));
        possibleCasts.put(
                TypeSpecSimple.DOUBLE,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.FLOAT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.NUMERIC,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.BIGINT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC)));
        possibleCasts.put(
                TypeSpecSimple.INT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.SHORT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.STRING,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.DATETIME,
                                TypeSpecSimple.DATE,
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.OBJECT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.DATETIME,
                                TypeSpecSimple.DATE,
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
    }
}