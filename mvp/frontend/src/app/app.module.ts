import { NgModule } from '@angular/core';
import { BrowserModule } from '@angular/platform-browser';
import { HttpClientModule } from '@angular/common/http';
import { FormsModule } from '@angular/forms';
import { CommonModule } from '@angular/common';

import { AppComponent } from './app.component';
import { CesiumViewerComponent } from './cesium-viewer/cesium-viewer.component';
import { ScenarioLoaderComponent } from './scenario-loader/scenario-loader.component';

@NgModule({
  declarations: [
    AppComponent,
    CesiumViewerComponent,
    ScenarioLoaderComponent,
  ],
  imports: [
    BrowserModule,
    HttpClientModule,
    FormsModule,
    CommonModule,
  ],
  bootstrap: [AppComponent],
})
export class AppModule {}
