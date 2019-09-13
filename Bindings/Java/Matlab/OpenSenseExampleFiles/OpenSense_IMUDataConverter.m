%% IMUDataConverter.m
% Example code for reading, and converting, XSENS IMU sensor data to
% OpenSense friendly format.
% Run this script from the OpenSenseExampleFiles directory.

% ----------------------------------------------------------------------- %
% The OpenSim API is a toolkit for musculoskeletal modeling and           %
% simulation. See http://opensim.stanford.edu and the NOTICE file         %
% for more information. OpenSim is developed at Stanford University       %
% and supported by the US National Institutes of Health (U54 GM072970,    %
% R24 HD065690) and by DARPA through the Warrior Web program.             %
%                                                                         %
% Copyright (c) 2005-2019 Stanford University and the Authors             %
% Author(s): James Dunne, Ajay Seth, Ayman Habib, Jen Hicks, Chris Dembia %
%                                                                         %
% Licensed under the Apache License, Version 2.0 (the "License");         %
% you may not use this file except in compliance with the License.        %
% You may obtain a copy of the License at                                 %
% http://www.apache.org/licenses/LICENSE-2.0.                             %
%                                                                         %
% Unless required by applicable law or agreed to in writing, software     %
% distributed under the License is distributed on an "AS IS" BASIS,       %
% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or         %
% implied. See the License for the specific language governing            %
% permissions and limitations under the License.                          %
% ----------------------------------------------------------------------- % 

%% Clear any variables in the workspace
clear all; close all; clc;

%% Import OpenSim libraries
import org.opensim.modeling.*

%% Build an Xsens Settings Object.
% Instantiate the Reader Settings Class
xsensSettings = XsensDataReaderSettings('myIMUMappings.xml');
% Instantiate an XsensDataReader
xsens = XsensDataReader(xsensSettings);
% Get a table reference for the data
tables = xsens.read('IMUData/');
% get the trial name from the settings
trial = char(xsensSettings.get_trial_prefix());

%% Get Orientation Data as quaternions
quatTable = xsens.getOrientationsTable(tables);
% Write to file
STOFileAdapterQuaternion.write(quatTable,  [trial '_orientations.sto']);

%% Get Acceleration Data
accelTable = xsens.getLinearAccelerationsTable(tables);
% Write to file
STOFileAdapterVec3.write(accelTable, [trial '_linearAccelerations.sto']);

%% Get Magnetic (North) Heading Data
magTable = xsens.getMagneticHeadingTable(tables);
% Write to file
STOFileAdapterVec3.write(magTable, [trial '_magneticNorthHeadings.sto']);

%% Get Angular Velocity Data
angVelTable = xsens.getAngularVelocityTable(tables);
% Write to file
STOFileAdapterVec3.write(angVelTable, [trial '_angularVelocities.sto']);